from pacman.model.decorators import overrides
from pacman.model.graphs.machine import MachineVertex
from pacman.model.resources import CPUCyclesPerTickResource, DTCMResource
from pacman.model.resources import ResourceContainer, SDRAMResource

from spinn_front_end_common.utilities import globals_variables
from spinn_front_end_common.utilities import constants, helpful_functions
from spinn_front_end_common.interface.simulation import simulation_utilities
from spinn_front_end_common.abstract_models.impl \
    import MachineDataSpecableVertex
from spinn_front_end_common.abstract_models import AbstractHasAssociatedBinary
from spinn_front_end_common.interface.buffer_management.buffer_models\
    import AbstractReceiveBuffersToHost
from spinn_front_end_common.interface.buffer_management\
    import recording_utilities
from spinn_front_end_common.utilities.utility_objs import ExecutableStartType

from enum import Enum
import logging

logger = logging.getLogger(__name__)

PARTITION_ID = "DATA"


class TemplateVertex(
        MachineVertex, MachineDataSpecableVertex, AbstractHasAssociatedBinary,
        AbstractReceiveBuffersToHost):

    # The number of bytes for the has_key flag and the key
    TRANSMISSION_REGION_N_BYTES = 2 * 4

    # TODO: Update with the regions of the application
    DATA_REGIONS = Enum(
        value="DATA_REGIONS",
        names=[('SYSTEM', 0),
               ('TRANSMISSION', 1),
               ('RECORDED_DATA', 2)])

    def __init__(self, label, constraints=None):

        self._recording_size = 5000

        MachineVertex.__init__(self, label=label, constraints=constraints)

        config = globals_variables.get_simulator().config
        self._buffer_size_before_receive = None
        if config.getboolean("Buffers", "enable_buffered_recording"):
            self._buffer_size_before_receive = config.getint(
                "Buffers", "buffer_size_before_receive")
        self._time_between_requests = config.getint(
            "Buffers", "time_between_requests")
        self._receive_buffer_host = config.get(
            "Buffers", "receive_buffer_host")
        self._receive_buffer_port = helpful_functions.read_config_int(
            config, "Buffers", "receive_buffer_port")

        self.placement = None

    @property
    @overrides(MachineVertex.resources_required)
    def resources_required(self):
        resources = ResourceContainer(
            cpu_cycles=CPUCyclesPerTickResource(45),
            dtcm=DTCMResource(100),
            sdram=SDRAMResource(
                constants.SYSTEM_BYTES_REQUIREMENT +
                self.TRANSMISSION_REGION_N_BYTES))
        resources.extend(recording_utilities.get_recording_resources(
            [self._recording_size], self._receive_buffer_host,
            self._receive_buffer_port))
        return resources

    @overrides(AbstractHasAssociatedBinary.get_binary_file_name)
    def get_binary_file_name(self):
        return "c_template_vertex.aplx"

    @overrides(AbstractHasAssociatedBinary.get_binary_start_type)
    def get_binary_start_type(self):
        return ExecutableStartType.USES_SIMULATION_INTERFACE

    @overrides(MachineDataSpecableVertex.generate_machine_data_specification)
    def generate_machine_data_specification(
            self, spec, placement, machine_graph, routing_info, iptags,
            reverse_iptags, machine_time_step, time_scale_factor):
        """ Generate data

        :param placement: the placement object for the dsg
        :param machine_graph: the graph object for this dsg
        :param routing_info: the routing info object for this dsg
        :param iptags: the collection of iptags generated by the tag allocator
        :param reverse_iptags: the collection of reverse iptags generated by\
                the tag allocator
        """
        self.placement = placement

        # Create the data regions
        self._reserve_memory_regions(spec)

        # write simulation interface data
        spec.switch_write_focus(self.DATA_REGIONS.SYSTEM.value)
        spec.write_array(simulation_utilities.get_simulation_header_array(
            self.get_binary_file_name(), machine_time_step,
            time_scale_factor))

        # write recording data interface
        spec.switch_write_focus(self.DATA_REGIONS.RECORDED_DATA.value)
        spec.write_array(recording_utilities.get_recording_header_array(
            [self._recording_size], self._time_between_requests,
            self._buffer_size_before_receive, iptags))

        # Get the key, assuming all outgoing edges use the same key
        has_key = 0
        key = routing_info.get_first_key_from_pre_vertex(self, PARTITION_ID)
        if key is None:
            key = 0
        else:
            has_key = 1

        # Write the transmission region
        spec.switch_write_focus(self.DATA_REGIONS.TRANSMISSION.value)
        spec.write_value(has_key)
        spec.write_value(key)

        # End-of-Spec:
        spec.end_specification()

    def _reserve_memory_regions(self, spec):
        spec.reserve_memory_region(
            region=self.DATA_REGIONS.SYSTEM.value,
            size=constants.SYSTEM_BYTES_REQUIREMENT,
            label='systemInfo')
        spec.reserve_memory_region(
            region=self.DATA_REGIONS.TRANSMISSION.value,
            size=self.TRANSMISSION_REGION_N_BYTES, label="transmission")
        spec.reserve_memory_region(
            region=self.DATA_REGIONS.RECORDED_DATA.value,
            size=recording_utilities.get_recording_header_size(1),
            label="recording")

    def read(self, placement, buffer_manager):
        """ Get the recorded data

        :param placement: the location of this vertex
        :param buffer_manager: the buffer manager
        :return: The data read
        """
        data_pointer, is_missing_data = buffer_manager.get_data_for_vertex(
            placement, 0)
        if is_missing_data:
            logger.warn("Some data was lost when recording")
        record_raw = data_pointer.read_all()
        output = str(record_raw)
        return output

    @overrides(AbstractReceiveBuffersToHost.get_minimum_buffer_sdram_usage)
    def get_minimum_buffer_sdram_usage(self):
        return 1024

    @overrides(AbstractReceiveBuffersToHost.get_n_timesteps_in_buffer_space)
    def get_n_timesteps_in_buffer_space(self, buffer_space, machine_time_step):
        return recording_utilities.get_n_timesteps_in_buffer_space(
            buffer_space, 100)

    @overrides(AbstractReceiveBuffersToHost.get_recorded_region_ids)
    def get_recorded_region_ids(self):
        return [0]

    @overrides(AbstractReceiveBuffersToHost.get_recording_region_base_address)
    def get_recording_region_base_address(self, txrx, placement):
        return helpful_functions.locate_memory_region_for_placement(
            placement, self.DATA_REGIONS.RECORDED_DATA.value, txrx)

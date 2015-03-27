"""
HeatDemoVertexPartitioned
"""
# spinn front end common imports
from pacman.model.partitioned_graph.partitioned_vertex import PartitionedVertex
from pacman.model.resources.cpu_cycles_per_tick_resource import \
    CPUCyclesPerTickResource
from pacman.model.resources.dtcm_resource import DTCMResource
from pacman.model.resources.resource_container import ResourceContainer
from pacman.model.resources.sdram_resource import SDRAMResource
from spynnaker_graph_front_end.abstract_partitioned_data_specable_vertex \
    import AbstractPartitionedDataSpecableVertex


class HeatDemoVertexPartitioned(PartitionedVertex,
                                AbstractPartitionedDataSpecableVertex):
    """
    HeatDemoVertexPartitioned: a vertex peice for a heat demo.
    represnets a heat element.
    """

    CORE_APP_IDENTIFIER = 0xABCD
    _model_based_max_atoms_per_core = 1
    _model_n_atoms = 1

    def __init__(self, label, machine_time_step, time_scale_factor,
                 constraints=None):

        # resoruces used by a heat element vertex
        resoruces = ResourceContainer(cpu=CPUCyclesPerTickResource(45),
                                      dtcm=DTCMResource(34),
                                      sdram=SDRAMResource(23))

        PartitionedVertex.__init__(
            self, label=label, resources_required=resoruces,
            constraints=constraints)
        AbstractPartitionedDataSpecableVertex.__init__(self)
        self._machine_time_step = machine_time_step
        self._time_scale_factor = time_scale_factor

    def _write_basic_setup_info(self, spec, core_app_identifier, region_id):

        # Write this to the system region (to be picked up by the simulation):
        spec.switch_write_focus(region=region_id)
        spec.write_value(data=core_app_identifier)
        spec.write_value(data=self._machine_time_step * self._timescale_factor)
        spec.write_value(data=self._no_machine_time_steps)

    def get_binary_file_name(self):
        """

        :return:
        """
        return "heat_demo.aplx"

    def model_name(self):
        """

        :return:
        """
        return "Heat_Demo_Vertex"

    def generate_data_spec(
            self, placement, sub_graph, routing_info, hostname, report_folder,
            write_text_specs, application_run_time_folder):
        """

        :param placement:
        :param sub_graph:
        :param routing_info:
        :param hostname:
        :param report_folder:
        :param write_text_specs:
        :param application_run_time_folder:
        :return:
        """
        pass

//! imports
#include "spin1_api.h"
#include "common-typedefs.h"
#include <stdbool.h>
#include <data_specification.h>
#include <simulation.h>
#include <debug.h>
#include <circular_buffer.h>
#include <string.h>

/*! multicast routing keys to communicate with neighbours */
uint my_key;
uint has_key;

/*! buffer used to store spikes */
static circular_buffer input_buffer;
static uint32_t current_payload;
static uint32_t current_key;

static uint32_t N_PACKETS_PER_EDGE = 14;

//! weather specific data items
s3231 my_p;
s3231 my_v;
s3231 my_u;
s3231 my_cv;
s3231 my_cu;
s3231 my_z;
s3231 my_h;

//! constants
s3231 tdt;
s3231 dx;
s3231 dy;
s3231 fsdx;
s3231 fsdy;
s3231 alpha;

// weather receive data items
s3231 east_elements[] = {0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k};
s3231 north_east_elements[] = {0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k};
s3231 north_elements[] = {0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k};
s3231 north_west_elements[] = {0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k};
s3231 west_elements[] = {0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k};
s3231 south_west_elements[] = {0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k};
s3231 south_elements[] = {0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k};
s3231 south_east_elements[] = {0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k};

//! neighbour keys
uint32_t north_key;
uint32_t north_east_key;
uint32_t east_key;
uint32_t north_west_key;
uint32_t west_key;
uint32_t south_west_key;
uint32_t south_key;
uint32_t south_east_key;

//! recorded data items
uint32_t size_written = 0;

//! control value, which says how many timer ticks to run for before exiting
static uint32_t simulation_ticks = 0;
static uint32_t time = 0;
address_t address = NULL;

//! int as a bool to represent if this simulation should run forever
static uint32_t infinite_run;

// value for turning on and off interrupts
uint cpsr = 0;

// optimisation for doing multiplications in the logic code.
static const long unsigned fract POINT_5 = 0.5k;

// optimisation for doing multiplications in the logic code.
static const long unsigned fract POINT_025 = 0.25k;

// optimisation for doing divide in the logic code.
static const uint32_t EIGHT = 8.0k;

//! human readable definitions of each region in SDRAM
typedef enum regions_e {
    SYSTEM_REGION,
    TRANSMISSIONS,
    NEIGHBOUR_KEYS,
    INIT_STATE_VALUES,
    FINAL_STATES
} regions_e;

//! values for the priority for each callback
typedef enum callback_priorities{
    MC_PACKET = -1, SDP = 0, TIMER = 2
} callback_priorities;

//! human readable definitions of each element in the transmission region
typedef enum transmission_region_elements {
    HAS_KEY, MY_KEY
} transmission_region_elements;

//! \brief a possible way to avoid reading in issues.
typedef struct init_data_t{
    // my values
    s3231 my_u; s3231 my_v; s3231 my_p;
    // neighbour values
    s3231 north_u; s3231 north_v; s3231 north_p;
    s3231 north_east_u; s3231 north_east_v; s3231 north_east_p;
    s3231 east_u; s3231 east_v; s3231 east_p;
    s3231 south_east_u; s3231 south_east_v; s3231 south_east_p;
    s3231 south_u; s3231 south_v; s3231 south_p;
    s3231 south_west_u; s3231 south_west_v; s3231 south_west_p;
    s3231 west_u; s3231 west_v; s3231 west_p;
    s3231 north_west_u; s3231 north_west_v; s3231 north_west_p;
    // constants
    s3231 tdt; s3231 dx; s3231 dy; s3231 fsdx; s3231 fsdy; s3231 alpha;
}init_data_t;

//! human readable for the key allocation offset for the data bits
typedef enum key_offsets {
    U_TOP_BIT = 0, U_FLOAT_BIT = 1, V_TOP_BIT = 2, V_FLOAT_BIT = 3,
    P_TOP_BIT = 4, P_FLOAT_BIT = 5, Z_TOP_BIT = 6, Z_FLOAT_BIT = 7,
    H_TOP_BIT = 8, H_FLOAT_BIT = 9, CV_TOP_BIT = 10, CV_FLOAT_BIT = 11,
    CU_TOP_BIT = 12, CU_FLOAT_BIT = 13
} key_offsets;

//! human readable for the location in a array for element bits
typedef enum element_offsets {
    U = 0, V = 1, P = 2, Z = 3, H = 4, CV = 5, CU = 6
} element_offsets;

//! human readable for the location of keys in sdram
typedef enum key_order {
    NORTH = 0, NORTH_EAST = 1, EAST = 2, NORTH_WEST = 3, WEST = 4,
    SOUTH_WEST = 5, SOUTH = 6, SOUTH_EAST = 7
} key_order;


//! \brief function to read in a s3231 from 2 ints
//! \param[in] top_bit: the top bit of the s3231 int
//! \param[in] float_bit: the float bit of the s3231 int
//! \param[out] the s3231 value
s3231 translate_to_s3231_value(uint32_t top_bit, uint32_t float_bit){
    s3231 my_top_bit = ((s3231)top_bit) << 32;
    s3231 my_point_bit = (s3231)float_bit;
    s3231 my_value = my_top_bit + my_point_bit;
    return my_value;
}

//! \brief converts a s3231 to the int bit for printing
//! \param[in] the s3231 to convert
//! \param[out] the int bit of the s3231
int32_t convert_s3231_to_int_bit(s3231 value_to_convert){
    return (int32_t)(value_to_convert >> 32);
}

//! \brief converts a s3231 to the long fract bit for printing
//! \param[in] the s3231 to convert
//! \param[out] the fract bit of the s3231
unsigned long fract convert_s3231_to_long_fract_bit(s3231 value_to_convert){
    return (unsigned long fract)(value_to_convert);
}

//! \brief converts a s3231 to the fract bit for printing
//! \param[in] the s3231 to convert
//! \param[out] the fract bit of the s3231
unsigned fract convert_s3231_to_fract_bit(s3231 value_to_convert){
    return (unsigned fract)(value_to_convert);
}


//! \brief prints a s3231 to the logger
//! \param[in] string: the text name for the value
//! \param[in] value_to_print: the s3231 value to print
void print(const char * string, s3231 value_to_print){
    log_info("%s = %d.%R", string, convert_s3231_to_int_bit(value_to_print),
                           convert_s3231_to_fract_bit(value_to_print));
}

//! \brief print the constants of the vertex
void print_constants(){
    log_info("doing constants");
    s3231 to_print_items[] = {tdt, dx, dy, fsdx, fsdy, alpha};
    const char *to_print_strings[6];
    to_print_strings[0] = "tdt";
    to_print_strings[1] = "dx";
    to_print_strings[2] = "dy";
    to_print_strings[3] = "fsdx";
    to_print_strings[4] = "fsdy";
    to_print_strings[5] = "alpha";
    for(int position = 0; position < 6; position ++){
        print(to_print_strings[position], to_print_items[position]);
    }
    log_info("end constants");
}

//! \brief print my local states
void print_my_states(){
    s3231 to_print_items[] = {my_u, my_v, my_p};
    const char *to_print_strings[3];
    to_print_strings[0] = "my_u";
    to_print_strings[1] = "my_v";
    to_print_strings[2] = "my_p";
    for(int position = 0; position < 3; position++){
        print(to_print_strings[position], to_print_items[position]);
    }
    log_info("my key = %d", my_key);
}

//! \brief print a set of elements for u,v,p
//! \param[in] elements_to_print: the array of elements to print elements of
//! \param[in] name: the name of the set of elements.
void print_a_set_of_elements(s3231 *elements_to_print, char* name){
    s3231 to_print_items[] = {elements_to_print[U], elements_to_print[V],
                             elements_to_print[P]};
    const char *to_print_strings[3];

    char copy_name[80];
    to_print_strings[0] = strcat(strcpy(copy_name, name), ":u");
    to_print_strings[1] = strcat(strcpy(copy_name, name), ":v");
    to_print_strings[2] = strcat(strcpy(copy_name, name), ":p");
    for(int position = 0; position < 3; position ++){
        print(to_print_strings[position], to_print_items[position]);
    }
}

//! \brief prints the set of elements from neighbour locations.
void print_elements(){
    char name[11];
    strcpy(name, "north");
    print_a_set_of_elements(north_elements, name);
    strcpy(name, "north_east");
    print_a_set_of_elements(north_east_elements, name);
    strcpy(name, "east");
    print_a_set_of_elements(east_elements, name);
    strcpy(name, "south_east");
    print_a_set_of_elements(south_east_elements, name);
    strcpy(name, "south");
    print_a_set_of_elements(south_elements, name);
    strcpy(name, "south_west");
    print_a_set_of_elements(south_west_elements, name);
    strcpy(name, "west");
    print_a_set_of_elements(west_elements, name);
    strcpy(name, "north_west");
    print_a_set_of_elements(north_west_elements, name);
}


//! \brief method for receiving multicast packets with payloads
//! \param[in] key: the multicast key
//! \param[in] payload: the multicast payload.
void receive_data(uint key, uint payload) {
    log_info("the key i've received is %d\n", key);
    log_info("the payload i've received is %d\n", payload);
    // If there was space to add data to incoming data queue
    if (!circular_buffer_add(input_buffer, key)) {
        log_info("Could not add state");
    }
    if (!circular_buffer_add(input_buffer, payload)) {
        log_info("Could not add state");
    }
}

//! \brief converts the 2 uint32s for each element from the directions and
//! converts them into the s3231 elements
//! \param[in] components: the set of 2 uint32's for the final elements
//! \param[in] final_elements: the final s3231's for the direction
//! \param[in] compass: which direction its dealing with now.
void convert_inputs_to_s3231_elements(
        s3231 *components, s3231 *final_elements,
        uint32_t* has_component_flags, char *compass){

    // try to deal with u
    if (has_component_flags[U_TOP_BIT] == 1 &&
            has_component_flags[U_FLOAT_BIT] == 1){
        final_elements[U] = translate_to_s3231_value(
            components[U_TOP_BIT], components[U_FLOAT_BIT]);
    }
    else{
         log_error("missing a packet for the u from the %s", compass);
    }

    // try to deal with v
    if (has_component_flags[V_TOP_BIT] == 1 &&
            has_component_flags[V_FLOAT_BIT] == 1){
        final_elements[V] = translate_to_s3231_value(
            components[V_TOP_BIT], components[V_FLOAT_BIT]);
    }else{
         log_error("missing a packet for the v from the %s", compass);
    }

    // try to deal with p
    if (has_component_flags[P_TOP_BIT] == 1 &&
            has_component_flags[P_FLOAT_BIT] == 1){
        final_elements[P] = translate_to_s3231_value(
            components[P_TOP_BIT], components[P_FLOAT_BIT]);
    }else{
         log_error("missing a packet for the p from the %s", compass);
    }

    // try to deal with Z
    if (has_component_flags[Z_TOP_BIT] == 1 &&
            has_component_flags[Z_FLOAT_BIT] == 1){
        final_elements[Z] = translate_to_s3231_value(
            components[Z_TOP_BIT], components[Z_FLOAT_BIT]);
    }else{
         log_error("missing a packet for the z from the %s", compass);
    }

    // try to deal with H
    if (has_component_flags[H_TOP_BIT] == 1 &&
            has_component_flags[H_FLOAT_BIT] == 1){
        final_elements[H] = translate_to_s3231_value(
            components[H_TOP_BIT], components[H_FLOAT_BIT]);
    }else{
         log_error("missing a packet for the h from the %s", compass);
    }

    // try to deal with cv
    if (has_component_flags[CV_TOP_BIT] == 1 &&
            has_component_flags[CV_FLOAT_BIT] == 1){
        final_elements[CV] = translate_to_s3231_value(
            components[CV_TOP_BIT], components[CV_FLOAT_BIT]);
    }else{
         log_error("missing a packet for the cv from the %s", compass);
    }

    // try to deal with cu
    if (has_component_flags[CU_TOP_BIT] == 1 &&
            has_component_flags[CU_FLOAT_BIT] == 1){
        final_elements[CU] = translate_to_s3231_value(
            components[CU_TOP_BIT], components[CU_FLOAT_BIT]);
    }else{
         log_error("missing a packet for the cu from the %s", compass);
    }
}


//! \brief puts the element in the correct location
//! \param[in] elements: the list of which this payload is going to reside
void process_key_payload(s3231 *elements, uint32_t *has_flag_elements){
    // get the offset in the array from the key
    uint32_t offset = current_key & 0xFFFFFFF0;

    // add the element to the correct offset
    elements[offset] = (s3231) current_payload;
    has_flag_elements[offset] = 1;
}


//! \brief reads in the ring buffer to get all the packets needed for the run
void read_input_buffer(){

    circular_buffer_print_buffer(input_buffer);

    // calculate how many packets should be in the buffer
    uint32_t n_packets_per_timer_tick = 14 * 8;

    while(circular_buffer_size(input_buffer) < n_packets_per_timer_tick){
        for(uint32_t counter=0; counter < 10000; counter++){
            //do nothing
        }
        log_info("size of buffer is %d whereas it should be %d",
                 circular_buffer_size(input_buffer), n_packets_per_timer_tick);
    }
    log_info("running past buffer length");

    cpsr = spin1_int_disable();
    circular_buffer_print_buffer(input_buffer);

    // linked to the key offset map
    s3231 east_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};
    uint32_t has_east_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};

    // linked to the key offset map
    s3231 north_east_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};
    uint32_t has_north_east_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};

    // linked to the key offset map
    s3231 north_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};
    uint32_t has_north_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};

    // linked to the key offset map
    s3231 north_west_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};
    uint32_t has_north_west_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};

    // linked to the key offset map
    s3231 west_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};
    uint32_t has_west_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};

    // linked to the key offset map
    s3231 south_west_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};
    uint32_t has_south_west_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};

    // linked to the key offset map
    s3231 south_east_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};
    uint32_t has_south_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};

    // linked to the key offset map
    s3231 south_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};
    uint32_t has_south_east_components[] = {
        0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k, 0.0k,
        0.0k, 0.0k, 0.0k};

    // pull payloads and keys from input_buffer.
    // translate into s3231 elements
    for (uint32_t counter = 0; counter < n_packets_per_timer_tick; counter ++){
        bool success1 = circular_buffer_get_next(
            input_buffer, &current_payload);
        bool success2 = circular_buffer_get_next(
            input_buffer, &current_key);
        if (success1 && success2){

            // deduce what the base key is
            uint32_t key_mask = 0xFFFFFFF0;
            uint32_t masked_key = current_key & key_mask;

            // process into the correct location
            if (masked_key == north_key){
                process_key_payload(
                    north_components, has_north_components);
            }
            else if (masked_key == north_east_key){
                process_key_payload(
                    north_east_components, has_north_east_components);
            }
            else if (masked_key == east_key){
                process_key_payload(east_components, has_east_components);
            }
            else if (masked_key == south_east_key){
                process_key_payload(
                    south_east_components, has_south_east_components);
            }
            else if (masked_key == south_key){
                process_key_payload(south_components, has_south_components);
            }
            else if (masked_key == south_west_key){
                process_key_payload(
                    south_west_components, has_south_west_components);
            }
            else if (masked_key == north_west_key){
                process_key_payload(
                    north_west_components, has_north_west_components);
            }
            else if (masked_key == west_key){
                process_key_payload(west_components, has_west_components);
            }

        }
        else{
            log_debug("couldn't read state from my neighbours.");
        }

    }
    spin1_mode_restore(cpsr);

    // convert 2 ints into s3231 for the correct locations.
    // handle north first
    convert_inputs_to_s3231_elements(
        north_components, north_elements,
        has_north_east_components, "north");
    convert_inputs_to_s3231_elements(
        north_east_components, north_east_elements,
        has_north_east_components, "north_east");
    convert_inputs_to_s3231_elements(
        east_components, east_elements,
        has_east_components, "east");
    convert_inputs_to_s3231_elements(
        south_east_components, south_east_elements,
        has_south_east_components, "south_east");
    convert_inputs_to_s3231_elements(
        south_components, south_elements,
        has_south_components, "south");
    convert_inputs_to_s3231_elements(
        south_west_components, south_west_elements,
        has_south_west_components, "south_west");
    convert_inputs_to_s3231_elements(
        west_components, west_elements,
        has_west_components, "west");
    convert_inputs_to_s3231_elements(
        north_west_components, north_west_elements,
        has_north_west_components, "north_west");
}

//! \brief records the data into the recording region
void record_state(){
    // record my state via sdram
    address_t record_region =
        data_specification_get_region(FINAL_STATES, address);
    uint8_t* record_space_address = (uint8_t*) record_region;
    record_space_address = record_space_address + 4 + size_written;
    spin1_memcpy(record_space_address, &my_p, 4);
    size_written = size_written + 4;
    log_debug("space written is %d", size_written);
    log_debug("recorded my state \n");
}

//! \brief sends the states needed for the neighbours to do their calculation.
void send_states(){
    // send my new state to the simulation neighbours
    uint32_t elements_to_send[14] = {
        convert_s3231_to_int_bit(my_u),
        bitsulr(convert_s3231_to_long_fract_bit(my_u)),
        convert_s3231_to_int_bit(my_v),
        bitsulr(convert_s3231_to_long_fract_bit(my_v)),
        convert_s3231_to_int_bit(my_p),
        bitsulr(convert_s3231_to_long_fract_bit(my_p)),
        convert_s3231_to_int_bit(my_z),
        bitsulr(convert_s3231_to_long_fract_bit(my_z)),
        convert_s3231_to_int_bit(my_h),
        bitsulr(convert_s3231_to_long_fract_bit(my_h)),
        convert_s3231_to_int_bit(my_cv),
         bitsulr(convert_s3231_to_long_fract_bit(my_cv)),
        convert_s3231_to_int_bit(my_cu),
        bitsulr(convert_s3231_to_long_fract_bit(my_cu))
    };

    const char *to_print_strings[14];
    to_print_strings[0] = "top_bit_u";
    to_print_strings[1] = "point_bit_u";
    to_print_strings[2] = "top_bit_v";
    to_print_strings[3] = "point_bit_v";
    to_print_strings[4] = "top_bit_p";
    to_print_strings[5] = "point_bit_p";
    to_print_strings[6] = "top_bit_z";
    to_print_strings[7] = "point_bit_z";
    to_print_strings[8] = "top_bit_h";
    to_print_strings[9] = "point_bit_h";
    to_print_strings[10] = "top_bit_cv";
    to_print_strings[11] = "point_bit_cv";
    to_print_strings[12] = "top_bit_cu";
    to_print_strings[13] = "point_bit_cu";

    for(uint32_t position = 0; position < N_PACKETS_PER_EDGE; position++){
        // log what we're firing
        log_info(
            "firing packet %s with value %d",
            to_print_strings[position], elements_to_send[position]);

        while (!spin1_send_mc_packet(
                my_key & position, elements_to_send[position], WITH_PAYLOAD)) {
            spin1_delay_us(1);
        }
    }
    log_debug("sent my states via multicast");
}


//! \brief calculates the cu for this atom
void calculate_cu(){
    my_cu = POINT_5 * (my_p + south_elements[P]) * my_u;
}

//! \brief calculates the cv for this atom
void calculate_cv(){
    my_cv = POINT_5 * (my_p + west_elements[P] * my_v);
}


//! \brief calculates the z for this atom
void calculate_z(){
    s3231 numerator_bit =
        (fsdx * (my_v - south_elements[V]) - fsdy * (my_u - west_elements[U]));
    s3231 denominator_bit =
        (south_west_elements[P] + west_elements[P] + my_p + south_elements[P]);
    my_z = numerator_bit / denominator_bit;
}


//! \brief calculates the h for this atom
void calculate_h(){
    my_h = my_p + POINT_025 * (
        north_elements[U] * north_elements[U] + my_u * my_u +
        east_elements[V] * my_v * my_v);
}


//! \brief calculates the new u,v,p for this atom
void calculate_new_internal_states(){
    // TODO move these to constants from python
    //s3231 tdts8 = tdt / EIGHT;
    //s3231 tdtsdx = tdt / dx;
    //s3231 tdtsdy = tdt / dx;



}


//! \brief is the timer tick callback function. handles auto pause and resume,
//! as well as the reading of the input buffer, updating states and sending
//! new states to neighbours
void update(uint ticks, uint b) {
    use(b);
    use(ticks);

    time++;

    log_info("on tick %d of %d", time, simulation_ticks);

    // check that the run time hasn't already elapsed and thus needs to be
    // killed
    if ((infinite_run != TRUE) && (time >= simulation_ticks)) {

        // update recording data
        address_t record_region =
            data_specification_get_region(FINAL_STATES, address);
        uint8_t* record_space_address = (uint8_t*) record_region;
        log_info("wrote final store of %d bytes", size_written);
        spin1_memcpy(record_space_address, &size_written, 4);

        log_info("Simulation complete.\n");

        // falls into the pause resume mode of operating
        simulation_handle_pause_resume(NULL);

        return;
    }

    if (time > 1){
        read_input_buffer();
    }

    calculate_cu();
    calculate_cv();
    calculate_z();
    calculate_h();

    calculate_new_internal_states();

    send_states();

        //record_state();
}


//! \brief this method is to catch strange behaviour
//! \param[in] key: the key being received
//! \param[in] unknown: second arg with no state. set to zero by default
void receive_data_void(uint key, uint unknown) {
    use(key);
    use(unknown);
    log_error("this should never ever be done\n");
}


//! \brief sets my key from sdram
//! \param[in] address: the top level address location for dsg region data
void set_key(address_t address){
    // initialise transmission keys
    address_t transmission_region_address = data_specification_get_region(
            TRANSMISSIONS, address);
    has_key = transmission_region_address[HAS_KEY];
    if (has_key == TRUE) {
        my_key = transmission_region_address[MY_KEY];
    }
}

//! \brief reads in the init states from sdram
//! \param[in] the address in sdram for the dsg region where this stuff starts
void set_init_states(address_t address){
    // read in the initial states
    // read my state
    address_t my_state_region_address = data_specification_get_region(
        INIT_STATE_VALUES, address);

    // converts sdram data into s3231 data types for easier usage during
    // transfer to dtcm data items
    init_data_t *init_data =  (init_data_t*) my_state_region_address;

    // this cores initial states
    my_p = init_data->my_p;
    my_u = init_data->my_u;
    my_v = init_data->my_v;

    // north initial states
    north_elements[U] = init_data->north_u;
    north_elements[V] = init_data->north_v;
    north_elements[P] = init_data->north_p;

    // north east initial states
    north_east_elements[U] = init_data->north_east_u;
    north_east_elements[V] = init_data->north_east_v;
    north_east_elements[P] = init_data->north_east_p;

    // east initial states
    east_elements[U] = init_data->east_u;
    east_elements[V] = init_data->east_v;
    east_elements[P] = init_data->east_p;

    // south east initial states
    south_east_elements[U] = init_data->south_east_u;
    south_east_elements[V] = init_data->south_east_v;
    south_east_elements[P] = init_data->south_east_p;

    // south initial states
    south_elements[U] = init_data->south_u;
    south_elements[V] = init_data->south_v;
    south_elements[P] = init_data->south_p;

    // south west initial states
    south_west_elements[U] = init_data->south_west_u;
    south_west_elements[V] = init_data->south_west_v;
    south_west_elements[P] = init_data->south_west_p;

    // west initial states
    west_elements[U] = init_data->west_u;
    west_elements[V] = init_data->west_v;
    west_elements[P] = init_data->west_p;

    // north west initial states
    north_west_elements[U] = init_data->north_west_u;
    north_west_elements[V] = init_data->north_west_v;
    north_west_elements[P] = init_data->north_west_p;

    // get constants
    tdt = init_data->tdt;
    dx = init_data->dx;
    dy = init_data->dy;
    fsdx = init_data->fsdx;
    fsdy = init_data->fsdy;
    alpha = init_data->alpha;

    // print out the values
    print_my_states();
    print_elements();
    print_constants();
}


//! \brief reads in the keys expected from neighbours from sdram.
void set_neighbour_keys(address_t address){
    
    address_t my_neighbour_state_region_address =
        data_specification_get_region(NEIGHBOUR_KEYS, address);
    north_key = my_neighbour_state_region_address[SOUTH];
    north_east_key = my_neighbour_state_region_address[SOUTH_WEST];
    east_key = my_neighbour_state_region_address[WEST];
    south_east_key = my_neighbour_state_region_address[SOUTH_EAST];
    south_key = my_neighbour_state_region_address[SOUTH];
    south_west_key = my_neighbour_state_region_address[SOUTH_WEST];
    west_key = my_neighbour_state_region_address[WEST];
    north_west_key = my_neighbour_state_region_address[NORTH_WEST];
}

//! \brief starts up the core, reading in all the init data from sdram.
//! \param[in] timer_period. the pointer for the timer period (set in sdram)
//! \param[out] bool which states if the init has been successful or not
static bool initialize(uint32_t *timer_period) {
    log_info("Initialise: started\n");

    // Get the address this core's DTCM data starts at from SDRAM
    address = data_specification_get_data_address();

    // Read the header
    if (!data_specification_read_header(address)) {
        log_error("failed to read the data spec header");
        return false;
    }

    // Get the timing details and set up the simulation interface
    if (!simulation_initialise(
            data_specification_get_region(SYSTEM_REGION, address),
            APPLICATION_NAME_HASH, timer_period, &simulation_ticks,
            &infinite_run, SDP, NULL, NULL)) {
        return false;
    }

    // find the key to use
    log_info("set key");
    set_key(address);

    // read in initials states
    log_info("set init states");
    set_init_states(address);

    // read neighbour keys
    log_info("set neighbour keys");
    set_neighbour_keys(address);

    // initialise my input_buffer for receiving packets
    log_info("build buffer");
    input_buffer = circular_buffer_initialize(512);
    if (input_buffer == 0){
        return false;
    }
    log_info("input_buffer initialised");

    return true;
}

//! \brief main entrance method. initialises this core and then sets up the
//! callbacks and event driven functions. Then finally calls run.
void c_main() {
    log_info("starting weather cell\n");

    // Load DTCM data
    uint32_t timer_period;

    // initialise the model
    if (!initialize(&timer_period)) {
        log_error("Error in initialisation - exiting!");
        rt_error(RTE_SWERR);
    }

    // set timer tick value to configured value
    log_info("setting timer to execute every %d microseconds", timer_period);
    spin1_set_timer_tick(timer_period);

    // register callbacks
    log_info("setting multicast packet with payload receiver callback");
    spin1_callback_on(MCPL_PACKET_RECEIVED, receive_data, MC_PACKET);

    log_info("setting the timer tick callback");
    spin1_callback_on(TIMER_TICK, update, TIMER);

    // start execution
    log_info("Starting\n");
    // Start the time at "-1" so that the first tick will be 0
    time = UINT32_MAX;

    simulation_run();
}
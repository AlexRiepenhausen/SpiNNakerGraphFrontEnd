
#ifndef __KEY_VALUE_COMMANDS_H__
    #define __KEY_VALUE_COMMANDS_H__

    #include "../../common/db-typedefs.h"
    #include "../../common/memory_utils.h"
    #include <debug.h>

    pullValue* key_value_commands_pull(
            address_t addr, uint32_t info, uchar* k){
        try(info);
        try(k);

        var_type k_type = k_type_from_info(info);
        size_t k_size   = k_size_from_info(info);

        info_t curr_info = 0;

        /* Get 32 bit information for the database entry
             4 bits key type,
             12 bits key size,
             4 bits value type, 12 bits value size */
        while((curr_info = *addr) != 0){

            addr++;

            var_type read_k_type = k_type_from_info(curr_info);
            size_t   read_k_size = k_size_from_info(curr_info);

            var_type v_type      = v_type_from_info(curr_info);
            size_t   v_size      = v_size_from_info(curr_info);

            if(read_k_size == 0 || v_size == 0){
                continue;
            }

            size_t k_size_words = (read_k_size+3) >> 2;
            size_t v_size_words = (v_size+3) >> 2;

            if(read_k_type != k_type || read_k_size != k_size){
                addr += k_size_words + v_size_words;
                continue;
            }

            uchar* k_found = (uchar*)addr;
            addr += k_size_words;

            uchar* v_found = (uchar*)addr;
            addr += v_size_words;

            if(memory_utils_arr_equals(k,k_found,k_size)){
                pullValue* v = (pullValue*)sark_alloc(1,sizeof(pullValue));
                v->size = v_size;
                v->type = v_type;
                sark_mem_cpy(v->data, v_found, v->size);

                return v;
            }
            else{
                continue;
            }
        }

        return NULL;
    }

    size_t key_value_commands_put(
            address_t* addr, info_t info, void* k, void* v){

        size_t k_size = k_size_from_info(info);
        size_t v_size = v_size_from_info(info);

        try(k_size && v_size && k && v);

        append(addr, &info, sizeof(info_t));
        append(addr, k,     k_size);
        append(addr, v,     v_size);

        return sizeof(info_t) +
               MULTIPLE_OF_4(k_size) +
               MULTIPLE_OF_4(v_size);
    }
#endif
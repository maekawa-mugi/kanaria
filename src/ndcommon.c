/*
 * Copyright (C) 2008-2012  OMRON SOFTWARE Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "nj_lib.h"
#include "nj_err.h"
#include "nj_ext.h"





int16_t njd_connection_matches(NJ_SEARCH_CONDITION *con, uint16_t left_connection_id, uint16_t right_connection_id)
{

    
    if (con->connection_filter.left_bitmap != NULL) {
        if (left_connection_id == 0) {
            return 0; 
        }

        left_connection_id--;
        if (left_connection_id >= con->connection_filter.left_count) {
            return 0; 
        }
        if (*(con->connection_filter.left_bitmap + (left_connection_id / 8)) & (0x80 >> (left_connection_id % 8))) {
            
            if (con->connection_filter.invert_left != 0) {
                
                return 0; 
            }
        } else {
            
            if (con->connection_filter.invert_left == 0) {
                
                return 0;
            }
        }
    }

    
    if (con->connection_filter.right_bitmap != NULL) {
        if (right_connection_id == 0) {
            return 0; 
        }

        right_connection_id--;
        if (right_connection_id >= con->connection_filter.right_count) {
            return 0; 
        }
        if (*(con->connection_filter.right_bitmap + (right_connection_id / 8)) & (0x80 >> (right_connection_id % 8))) {
            
            if (con->connection_filter.invert_right != 0) {
                
                return 0; 
            }
        } else {
            
            if (con->connection_filter.invert_right == 0) {
                
                return 0;
            }
        }
    }
    
    return 1;
}

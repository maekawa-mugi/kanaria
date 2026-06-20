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

#ifndef _NJX_LIB_H_
#define _NJX_LIB_H_


#define NJD_MAX_CONNECT_CNT     6

typedef struct {
    uint16_t left_connection_id;
    uint16_t right_connection_id;
    uint8_t  yomi_len;
    uint8_t  hyouki_len;
    NJ_CHAR   yomi[NJ_MAX_LEN +NJ_TERM_LEN];           
    NJ_CHAR   hyouki[NJ_MAX_RESULT_LEN + NJ_TERM_LEN]; 
    uint16_t stem_right_connection_id;
    uint8_t  fzk_yomi_len;
} NJ_LEARN_WORD_INFO;


typedef struct word_que {
    uint16_t  entry;
    uint8_t   type;
    uint16_t  left_connection_id;
    uint16_t  right_connection_id;
    uint8_t   yomi_len;
    uint8_t   hyouki_len;
    uint8_t   yomi_byte;
    uint8_t   hyouki_byte;
    uint8_t   next_flag;
} NJ_WQUE;


typedef struct {
    NJ_LEARN_WORD_INFO  selection_data;                             
    uint8_t            count;
} NJ_PREVIOUS_SELECTION_INFO;

typedef struct {
    
    
    
    
    NJ_WQUE que_tmp;

    
    
    
    
    NJ_PREVIOUS_SELECTION_INFO previous_selection;

    
    
    
    
    NJ_CHAR learn_string_tmp[NJ_MAX_RESULT_LEN + NJ_TERM_LEN];
    
    NJ_CHAR muhenkan_tmp[NJ_MAX_RESULT_LEN + NJ_TERM_LEN];

    
    
    
    NJ_DIC_SET dic_set;         

    struct {
        uint8_t   commit_status;
        uint16_t  save_top;
        uint16_t  save_bottom;
        uint16_t  save_count;
    } learndic_status;

} NJ_CLASS;

#endif 

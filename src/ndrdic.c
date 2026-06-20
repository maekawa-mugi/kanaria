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
#include "nj_dic.h"
#include "njd.h"


#define LEFT_CONNECTION_TABLE_ADDR(h) ((h)+nj_read_le32((h)+0x20))
#define RIGHT_CONNECTION_TABLE_ADDR(h) ((h)+nj_read_le32((h)+0x24))
#define V2_LEFT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x28)))
#define BUNTOU_RIGHT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x2A)))
#define SINGLE_KANJI_LEFT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x30)))
#define SINGLE_KANJI_RIGHT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x32)))
#define NUMBER_RIGHT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x34)))
#define NOUN_LEFT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x36)))
#define NOUN_RIGHT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x38)))
#define PERSON_NAME_LEFT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x3A)))
#define PERSON_NAME_RIGHT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x3C)))
#define PLACE_NAME_LEFT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x3E)))
#define PLACE_NAME_RIGHT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x40)))
#define SYMBOL_LEFT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x42)))
#define SYMBOL_RIGHT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x44)))
#define V1_LEFT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x52)))
#define V3_LEFT_CONNECTION_ID(h) ((uint16_t)(nj_read_le16((h)+0x54)))

int16_t njd_r_get_connection_id(NJ_DIC_HANDLE rule, uint8_t type) {

    
    if (rule == NULL) {
        return 0; 
    }

    switch (type) {
    case NJ_CONNECTION_V2_LEFT :
        return V2_LEFT_CONNECTION_ID(rule);
    case NJ_CONNECTION_BUNTOU_RIGHT :
        return BUNTOU_RIGHT_CONNECTION_ID(rule);
    case NJ_CONNECTION_SINGLE_KANJI_LEFT :
        return SINGLE_KANJI_LEFT_CONNECTION_ID(rule);
    case NJ_CONNECTION_SINGLE_KANJI_RIGHT :
        return SINGLE_KANJI_RIGHT_CONNECTION_ID(rule);
    case NJ_CONNECTION_NUMBER_RIGHT:
        return NUMBER_RIGHT_CONNECTION_ID(rule);
    case NJ_CONNECTION_NOUN_LEFT :
        return NOUN_LEFT_CONNECTION_ID(rule);
    case NJ_CONNECTION_NOUN_RIGHT :
        return NOUN_RIGHT_CONNECTION_ID(rule);
    case NJ_CONNECTION_PERSON_NAME_LEFT :
        return PERSON_NAME_LEFT_CONNECTION_ID(rule);
    case NJ_CONNECTION_PERSON_NAME_RIGHT :
        return PERSON_NAME_RIGHT_CONNECTION_ID(rule);
    case NJ_CONNECTION_PLACE_NAME_LEFT :
        return PLACE_NAME_LEFT_CONNECTION_ID(rule);
    case NJ_CONNECTION_PLACE_NAME_RIGHT :
        return PLACE_NAME_RIGHT_CONNECTION_ID(rule);
    case NJ_CONNECTION_SYMBOL_LEFT :
        return SYMBOL_LEFT_CONNECTION_ID(rule);
    case NJ_CONNECTION_SYMBOL_RIGHT :
        return SYMBOL_RIGHT_CONNECTION_ID(rule);
    case NJ_CONNECTION_V1_LEFT :
        return V1_LEFT_CONNECTION_ID(rule);
    case NJ_CONNECTION_V3_LEFT :
        return V3_LEFT_CONNECTION_ID(rule);    default:
    
        return 0; 
    }
}

int16_t njd_r_get_connection_row(NJ_DIC_HANDLE rule, uint16_t connection_id, uint8_t type, const uint8_t **row) {
    uint16_t i, rec_len;

    
    if (rule == NULL) {
        return 0; 
    }
    if (connection_id < 1) {
        return 0;
    }

    if (type == NJ_RULE_TYPE_BTOF) {    
        i = LEFT_CONNECTION_COUNT(rule);
        rec_len = (uint16_t)((i + 7) / 8);
                                        
        *row = LEFT_CONNECTION_TABLE_ADDR(rule) + ((connection_id - 1) * rec_len);
    } else {                            
        i = RIGHT_CONNECTION_COUNT(rule);
        rec_len = (uint16_t)((i + 7) / 8);
                                        
        *row = RIGHT_CONNECTION_TABLE_ADDR(rule) + ((connection_id - 1) * rec_len);
    }
    return 0;
}

int16_t njd_r_get_connection_counts(NJ_DIC_HANDLE rule, uint16_t *left_count, uint16_t *right_count) {

    
    if (rule == NULL) {
        return 0; 
    }

    *left_count = LEFT_CONNECTION_COUNT(rule);
    *right_count = RIGHT_CONNECTION_COUNT(rule);

    return 0;
}

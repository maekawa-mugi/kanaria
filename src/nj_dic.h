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

#ifndef _NJ_DIC_H_
#define _NJ_DIC_H_

#define NJ_DIC_TYPE_JIRITSU                     0x00000000      
#define NJ_DIC_TYPE_FZK                         0x00000001      
#define NJ_DIC_TYPE_TANKANJI                    0x00000002      
#define NJ_DIC_TYPE_CUSTOM_COMPRESS             0x00000003      
#define NJ_DIC_TYPE_STDFORE                     0x00000004      
#define NJ_DIC_TYPE_FORECONV                    0x00000005      
#define NJ_DIC_TYPE_YOMINASHI                   0x00010000      
#define NJ_DIC_TYPE_CUSTOM_INCOMPRESS           0x00020002      
#define NJ_DIC_TYPE_USER                        0x80030000      
#define NJ_DIC_TYPE_RULE                        0x000F0000      

#define NJ_CONNECTION_V2_LEFT            0
#define NJ_CONNECTION_NUMBER_RIGHT        14
#define NJ_CONNECTION_BUNTOU_RIGHT        3
#define NJ_CONNECTION_SINGLE_KANJI_LEFT      4
#define NJ_CONNECTION_SINGLE_KANJI_RIGHT      5
#define NJ_CONNECTION_NOUN_LEFT         6
#define NJ_CONNECTION_NOUN_RIGHT         7
#define NJ_CONNECTION_PERSON_NAME_LEFT        8
#define NJ_CONNECTION_PERSON_NAME_RIGHT        9
#define NJ_CONNECTION_PLACE_NAME_LEFT       10
#define NJ_CONNECTION_PLACE_NAME_RIGHT       11
#define NJ_CONNECTION_SYMBOL_LEFT        12
#define NJ_CONNECTION_SYMBOL_RIGHT        13
#define NJ_CONNECTION_V1_LEFT           15
#define NJ_CONNECTION_V3_LEFT           16
#define NJ_RULE_TYPE_BTOF       0
#define NJ_RULE_TYPE_FTOB       1

#define NJD_SAME_INDEX_LIMIT    50

static inline uint16_t nj_read_le16(const uint8_t *in)
{
    return (uint16_t)in[0] | ((uint16_t)in[1] << 8);
}

static inline uint32_t nj_read_le32(const uint8_t *in)
{
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static inline void nj_write_le32(uint8_t *to, uint32_t from)
{
    to[0] = (uint8_t)from;
    to[1] = (uint8_t)(from >> 8);
    to[2] = (uint8_t)(from >> 16);
    to[3] = (uint8_t)(from >> 24);
}

static inline void nj_write_le16(uint8_t *to, uint16_t from)
{
    to[0] = (uint8_t)from;
    to[1] = (uint8_t)(from >> 8);
}

#define NJ_GET_MAX_YLEN(h) ((int16_t)(nj_read_le32((h)+0x14)/sizeof(NJ_CHAR)))

#define NJ_GET_MAX_KLEN(h) ((int16_t)(nj_read_le32((h)+0x18)/sizeof(NJ_CHAR)))

#define NJ_GET_DIC_TYPE(h) (nj_read_le32((h)+8))

#define LEFT_CONNECTION_COUNT(h) (nj_read_le16((h)+0x1C))
#define RIGHT_CONNECTION_COUNT(h) (nj_read_le16((h)+0x1E))


#endif 

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

#ifndef _NJ_EXTERN_H_
#define _NJ_EXTERN_H_


#define NJ_MAX_CHAR_LEN  2

#define NJ_CHAR_IS_HIGH_SURROGATE(c) ((c) >= 0xD800 && (c) <= 0xDBFF)
#define NJ_CHAR_IS_LOW_SURROGATE(c)  ((c) >= 0xDC00 && (c) <= 0xDFFF)

#define NJ_CHAR_IS_EQUAL(a, b) \
    (*(a) == *(b))

#define NJ_CHAR_IS_LESSEQ(a, b)                                         \
    (*(a) <= *(b))

#define NJ_CHAR_IS_MOREEQ(a, b)                                         \
    (*(a) >= *(b))

#define NJ_CHAR_DIFF(a, b)                                              \
    ((int16_t)((*(a) < *(b)) ? -1 : ((*(a) > *(b)) ? 1 : 0)))

#define NJ_CHAR_COPY(dst, src)                                          \
    {                                                                   \
        *(dst) = *(src);                                                \
        if (NJ_CHAR_LEN(src) == 2) {                                    \
            *((dst) + 1) = *((src) + 1);                               \
        }                                                               \
    }

#define NJ_CHAR_STRLEN_IS_0(c)   (*(c) == NJ_CHAR_NUL)

#define NJ_CHAR_ILLEGAL_DIC_YINDEX(size)   ((size) != sizeof(NJ_CHAR))


#define NJ_CHAR_LEN(s)                                                  \
    ((NJ_CHAR_IS_HIGH_SURROGATE(*(s)) &&                                \
      NJ_CHAR_IS_LOW_SURROGATE(*((s) + 1))) ? 2 : 1)

#define UTL_CHAR(s) NJ_CHAR_LEN(s)


#define NJ_GET_DIC_INFO(dicinfo) ((uint8_t)((dicinfo)->type))

#define NJ_GET_DIC_TYPE_EX(type, handle) \
                 NJ_GET_DIC_TYPE((handle))                                    


#define GET_BITFIELD_16(data, pos, width)                        \
    ((uint16_t)(((uint16_t)(data) >> (16 - (pos) - (width))) & \
                 ((uint16_t)0xffff >> (16 - (width)       ))))

#define GET_BITFIELD_32(data, pos, width)       \
    ((uint32_t)(((uint32_t)(data) >> (32 - (pos) - (width))) & ((uint32_t)0xffffffff >> (32 - (width)))))

#define GET_BIT_TO_BYTE(bit) ((uint8_t)(((bit) + 7) >> 3))


#define INIT_KEYWORD_IN_NJ_DIC_SET(x) \
    { (x)->keyword[0] = NJ_CHAR_NUL; (x)->keyword[1] = NJ_CHAR_NUL; }

#define GET_ERR_FUNCVAL(errval) \
    ((uint16_t)(((uint16_t)(errval) & 0x007F) << 8))

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t njd_get_word_data(NJ_CLASS *context, NJ_DIC_SET *dicset, NJ_SEARCH_LOCATION_SET *loctset, uint16_t dic_idx, NJ_WORD *word);
extern int16_t njd_get_stroke(NJ_CLASS *context, NJ_RESULT *result,
                               NJ_CHAR *stroke, uint16_t size);
extern int16_t njd_get_candidate(NJ_CLASS *context, NJ_RESULT *result,
                               NJ_CHAR *candidate, uint16_t size);
extern int16_t njd_init_search_location_set(NJ_SEARCH_LOCATION_SET* loctset);
extern int16_t njd_init_word(NJ_WORD* word);

extern int16_t njd_l_search_word(NJ_CLASS *context, NJ_SEARCH_CONDITION *con,
                                  NJ_SEARCH_LOCATION_SET *loctset, uint8_t comp_flg);
extern int16_t njd_l_get_word(NJ_CLASS *context, NJ_SEARCH_LOCATION_SET *loctset, NJ_WORD *word);
extern int16_t njd_l_get_stroke(NJ_CLASS *context, NJ_WORD *word,
                                 NJ_CHAR *stroke, uint16_t size);
extern int16_t njd_l_get_candidate(NJ_CLASS *context, NJ_WORD *word,
                                 NJ_CHAR *candidate, uint16_t size);
extern int16_t njd_l_check_dic(NJ_CLASS *context, NJ_DIC_HANDLE handle);

extern int16_t njd_r_get_connection_id(NJ_DIC_HANDLE rule, uint8_t type);
extern int16_t njd_r_get_connection_row(NJ_DIC_HANDLE rule,
                                  uint16_t connection_id, uint8_t type,
                                  const uint8_t **row);
extern int16_t njd_r_get_connection_counts(NJ_DIC_HANDLE rule,
                                uint16_t *left_count, uint16_t *right_count);

extern uint16_t nje_check_string(NJ_CHAR *s, uint16_t max_len);
extern uint8_t nje_get_top_char_type(NJ_CHAR *s);
extern int16_t nje_convert_kata_to_hira(NJ_CHAR *kata, NJ_CHAR *hira, uint16_t len, uint16_t max_len, uint8_t type);
extern int16_t nje_convert_hira_to_kata(NJ_CHAR *hira, NJ_CHAR *kata, uint16_t len);

extern int16_t njd_connection_matches(NJ_SEARCH_CONDITION *con, uint16_t left_connection_id, uint16_t right_connection_id);

extern NJ_CHAR  *nj_strcpy(NJ_CHAR *dst, NJ_CHAR *src);
extern NJ_CHAR  *nj_strncpy(NJ_CHAR *dst, NJ_CHAR *src, uint16_t n);
extern uint16_t nj_strlen(NJ_CHAR *c);
extern int16_t  nj_strcmp(NJ_CHAR *s1, NJ_CHAR *s2);
extern int16_t  nj_strncmp(NJ_CHAR *s1, NJ_CHAR *s2, uint16_t n);
extern uint16_t nj_charlen(NJ_CHAR *c);
extern int16_t  nj_charncmp(NJ_CHAR *s1, NJ_CHAR *s2, uint16_t n);
extern NJ_CHAR  *nj_charncpy(NJ_CHAR *dst, NJ_CHAR *src, uint16_t n);
extern uint8_t *nj_memcpy(uint8_t *dst, uint8_t *src, uint16_t n);

#ifdef __cplusplus
} // extern "C"
#endif

#endif 

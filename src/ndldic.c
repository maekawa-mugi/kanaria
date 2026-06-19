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
#ifdef NJ_LEARN_MUHENKAN_DEBUG
#include <stdio.h>
#include <def_mojicode.h>
#endif 
#ifdef NJ_AWNN22_DEBUG
#include <stdio.h>
#include <def_mojicode.h>
#endif 

#define QUE_TYPE_EMPTY  0   
#define QUE_TYPE_NEXT   0   
#define QUE_TYPE_JIRI   1   
#define QUE_TYPE_FZK    2   
#define POS_DATA_OFFSET  0x20
#define POS_LEARN_WORD   0x24    
#define POS_MAX_WORD     0x28    
#define POS_QUE_SIZE     0x2C    
#define POS_NEXT_QUE     0x30    
#define POS_WRITE_FLG    0x34    
#define POS_INDEX_OFFSET        0x3C
#define POS_INDEX_OFFSET2       0x40

#define LEARN_INDEX_TOP_ADDR(x) ((x) + (nj_read_le32((x) + POS_INDEX_OFFSET)))
#define LEARN_INDEX_TOP_ADDR2(x) ((x) + (nj_read_le32((x) + POS_INDEX_OFFSET2)))
#define LEARN_DATA_TOP_ADDR(x)  ((x) + (nj_read_le32((x) + POS_DATA_OFFSET)))

#define LEARN_INDEX_BOTTOM_ADDR(x) (LEARN_DATA_TOP_ADDR(x) - 1)

#define LEARN_QUE_STRING_OFFSET 5

#define ADDRESS_TO_POS(x,adr)   (((adr) - LEARN_DATA_TOP_ADDR(x)) / QUE_SIZE(x))
#define POS_TO_ADDRESS(x,pos)   (LEARN_DATA_TOP_ADDR(x) + QUE_SIZE(x) * (pos))

#define GET_UINT16(ptr) ((((uint16_t)(*((ptr) + 1))) << 8) | (*(ptr) & 0x00ff))

#define GET_FPOS_FROM_DATA(x) ((uint16_t)nj_read_le16((x)+1) >> 7)
#define GET_YSIZE_FROM_DATA(x) ((uint8_t)((uint16_t)nj_read_le16((x)+1) & 0x7F))
#define GET_BPOS_FROM_DATA(x) ((uint16_t)nj_read_le16((x)+3) >> 7)
#define GET_KSIZE_FROM_DATA(x) ((uint8_t)((uint16_t)nj_read_le16((x)+3) & 0x7F))
#define GET_BPOS_FROM_EXT_DATA(x) ((uint16_t)nj_read_le16(x) >> 7)
#define GET_YSIZE_FROM_EXT_DATA(x) ((uint8_t)((uint16_t)nj_read_le16(x) & 0x7F))

#define SET_BPOS_AND_YSIZE(x,bpos,ysize)                                \
    nj_write_le16((x), ((uint16_t)((bpos) << 7) | ((ysize) & 0x7F)))
#define SET_FPOS_AND_YSIZE(x,fpos,ysize)                                \
    nj_write_le16(((x)+1), ((uint16_t)((fpos) << 7) | ((ysize) & 0x7F)))
#define SET_BPOS_AND_KSIZE(x,bpos,ksize)                                \
    nj_write_le16(((x)+3), ((uint16_t)((bpos) << 7) | ((ksize) & 0x7F)))

#define GET_TYPE_FROM_DATA(x) (*(x) & 0x03)
#define GET_UFLG_FROM_DATA(x) (*(x) >> 7)
#define GET_FFLG_FROM_DATA(x) ((*(x) >> 6) & 0x01)
#define GET_MFLG_FROM_DATA(x) (*(x) & 0x10)

#define SET_TYPE_UFLG_FFLG(x,type,u,f)                                  \
    (*(x) = (uint8_t)(((type) & 0x03) |                                \
                       (((u) & 0x01) << 7) | (((f) & 0x01) << 6)))
#define SET_TYPE_ALLFLG(x,type,u,f,m)                                   \
    (*(x) = (uint8_t)(((type) & 0x03) |                                \
                       (((u) & 0x01) << 7) | (((f) & 0x01) << 6) | (((m) & 0x01) << 4)))

#define RESET_FFLG(x) (*(x) &= 0xbf)

#define STATE_COPY(to, from)                                    \
    { ((uint8_t*)(to))[0] = ((uint8_t*)(from))[0];            \
        ((uint8_t*)(to))[1] = ((uint8_t*)(from))[1];          \
        ((uint8_t*)(to))[2] = ((uint8_t*)(from))[2];          \
        ((uint8_t*)(to))[3] = ((uint8_t*)(from))[3]; }

#define USE_QUE_NUM(que_size, str_size)    \
    ( (((str_size) % ((que_size) - 1)) == 0)                           \
      ? ((str_size) / ((que_size) - 1))                                \
      : ((str_size) / ((que_size) - 1) + 1) )

#define NEXT_QUE(que, max)  ( ((que) < ((max) - 1)) ? ((que) + 1) : 0 )

#define PREV_QUE(que, max)  ( ((que) == 0) ? ((max) - 1) : ((que) - 1) )

#define COPY_QUE(handle, src, dst)                                      \
    nj_memcpy(POS_TO_ADDRESS((handle), (dst)), POS_TO_ADDRESS((handle), (src)), QUE_SIZE(handle))


#define INIT_HINDO          (-10000)

#define LOC_CURRENT_NO_ENTRY  0xffffffffU



static NJ_WQUE *get_que(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id);
static int16_t is_continued(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id);
static uint16_t search_next_que(NJ_DIC_HANDLE handle, uint16_t que_id);
static int16_t que_strcmp_complete_with_hyouki(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id, NJ_CHAR *yomi, uint16_t yomi_len, NJ_CHAR *hyouki, uint8_t multi_flg);
static NJ_CHAR  *get_string(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id, uint8_t *slen);
static NJ_CHAR  *get_hyouki(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id, uint8_t *slen);
static int16_t get_cand_by_sequential(NJ_CLASS *iwnn, NJ_SEARCH_CONDITION *cond, NJ_SEARCH_LOCATION_SET *loctset, uint8_t search_pattern, uint8_t comp_flg);
static int16_t get_cand_by_evaluate(NJ_CLASS *iwnn, NJ_SEARCH_CONDITION *cond, NJ_SEARCH_LOCATION_SET *loctset, uint8_t search_pattern);
static int16_t get_cand_by_evaluate2(NJ_CLASS *iwnn, NJ_SEARCH_CONDITION *cond, NJ_SEARCH_LOCATION_SET *loctset, uint8_t search_pattern, uint16_t hIdx);
static int16_t search_range_by_yomi(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint8_t op, NJ_CHAR *yomi, uint16_t ylen, uint16_t *from, uint16_t *to, uint8_t *forward_flag);
static int16_t search_range_by_yomi2(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint8_t op, NJ_CHAR *yomi, uint16_t ylen, uint16_t sfrom, uint16_t sto, uint16_t *from, uint16_t *to,
                                      uint8_t *forward_flag);
static int16_t search_range_by_yomi_multi(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, NJ_CHAR *yomi, uint16_t ylen, uint16_t *from, uint16_t *to);
static int16_t str_que_cmp(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, NJ_CHAR *yomi, uint16_t yomiLen, uint16_t que_id, uint8_t mode);
static NJ_WQUE *get_que_type_and_next(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id);
static NJ_WQUE *get_que_allHinsi(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id);
static NJ_WQUE *get_que_yomiLen_and_hyoukiLen(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id);
static int16_t continue_cnt(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id);

static uint8_t *get_search_index_address(NJ_DIC_HANDLE handle, uint8_t search_pattern);

static NJ_HINDO get_hindo(NJ_CLASS *iwnn, NJ_SEARCH_LOCATION_SET *loctset, uint8_t search_pattern);

static NJ_HINDO calculate_hindo(NJ_DIC_HANDLE handle, int32_t freq, NJ_DIC_FREQ *dic_freq, int16_t freq_max, int16_t freq_min);
static int16_t que_strcmp_include(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id, NJ_CHAR *yomi);

#define GET_LEARN_MAX_WORD_COUNT(h) ((uint16_t)nj_read_le32((h) + POS_MAX_WORD))

#define GET_LEARN_WORD_COUNT(h)                         \
    ((uint16_t)nj_read_le32((h) + POS_LEARN_WORD))
#define SET_LEARN_WORD_COUNT(h, n)                      \
    nj_write_le32((h)+POS_LEARN_WORD, (uint32_t)(n))
#define GET_LEARN_NEXT_WORD_POS(h)                      \
    ((uint16_t)nj_read_le32((h) + POS_NEXT_QUE))
#define SET_LEARN_NEXT_WORD_POS(h, id)                  \
    nj_write_le32((h)+POS_NEXT_QUE, (uint32_t)(id))
#define QUE_SIZE(h)     ((uint16_t)nj_read_le32((h) + POS_QUE_SIZE))

#define COPY_UINT16(dst,src)    (*(uint16_t *)(dst) = *(uint16_t *)(src))

static uint8_t *get_search_index_address(NJ_DIC_HANDLE handle, uint8_t search_pattern) {


    
    return LEARN_INDEX_TOP_ADDR(handle);
}

int16_t njd_l_search_word(NJ_CLASS *iwnn, NJ_SEARCH_CONDITION *con,
                           NJ_SEARCH_LOCATION_SET *loctset,
                           uint8_t comp_flg) {

    uint16_t    word_count;
    uint32_t    type;
    NJ_DIC_INFO *pdicinfo;
    uint16_t    hIdx;
    int16_t     ret;


    word_count = GET_LEARN_WORD_COUNT(loctset->loct.handle);
    if (word_count == 0) {
        
        loctset->loct.status = NJ_ST_SEARCH_END_EXT;
        return 0;
    }

    type = NJ_GET_DIC_TYPE_EX(loctset->loct.type, loctset->loct.handle);
    
    if (type == NJ_DIC_TYPE_CUSTOM_INCOMPRESS) {
        if ((con->operation == NJ_CUR_OP_COMP) ||
            (con->operation == NJ_CUR_OP_FORE)){
            
            if (con->ylen > NJ_GET_MAX_YLEN(loctset->loct.handle)) {
                loctset->loct.status = NJ_ST_SEARCH_END_EXT;
                return 0;
            }
        }
    }

    
    switch (con->operation) {
    case NJ_CUR_OP_COMP:
        if (con->mode != NJ_CUR_MODE_FREQ) {
            
            loctset->loct.status = NJ_ST_SEARCH_END_EXT;
            break;
        }
        
        
        return get_cand_by_sequential(iwnn, con, loctset, con->operation, comp_flg);

    case NJ_CUR_OP_FORE:
        
        if (con->mode == NJ_CUR_MODE_YOMI) {
            
            return get_cand_by_sequential(iwnn, con, loctset, con->operation, 0);
        } else {
            
            
            pdicinfo = con->ds->dic;
            for (hIdx = 0; (hIdx < NJ_MAX_DIC) && (pdicinfo->handle != loctset->loct.handle); hIdx++) {
                pdicinfo++;
            }

            if (hIdx == NJ_MAX_DIC) {
                
                loctset->loct.status = NJ_ST_SEARCH_END; 
                return 0; 
            }

            
            
            if ((con->ds->dic[hIdx].srhCache == NULL) || (con->ylen == 0) ||
                !(con->ds->mode & 0x0001)) {
                return get_cand_by_evaluate(iwnn, con, loctset, con->operation);
            } else {
                ret = get_cand_by_evaluate2(iwnn, con, loctset, con->operation, hIdx);
                if (ret == NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_CACHE_NOT_ENOUGH)) {
                    
                    NJ_SET_CACHEOVER_TO_SCACHE(con->ds->dic[hIdx].srhCache);
                    ret = get_cand_by_evaluate2(iwnn, con, loctset, con->operation, hIdx);
                }
                return ret;
            }
        }

    case NJ_CUR_OP_LINK:
        
        if (NJ_GET_DIC_TYPE_EX(loctset->loct.type, loctset->loct.handle) == NJ_DIC_TYPE_USER) {
            
            loctset->loct.status = NJ_ST_SEARCH_END_EXT;
            break;
        }
        if (con->mode != NJ_CUR_MODE_FREQ) {
            
            loctset->loct.status = NJ_ST_SEARCH_END_EXT;
            break;
        }
        
        if (comp_flg == 0) {
            
            return get_cand_by_sequential(iwnn, con, loctset, con->operation, 0);
        } else {
            
            return get_cand_by_evaluate(iwnn, con, loctset, con->operation);
        }

    default:
        loctset->loct.status = NJ_ST_SEARCH_END_EXT;
    }

    return 0;
}

static NJ_WQUE *get_que_type_and_next(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle,
                                      uint16_t que_id) {
    uint8_t *ptr;
    NJ_WQUE *que = &(iwnn->que_tmp);


    if (que_id >= GET_LEARN_MAX_WORD_COUNT(handle)) {
        return NULL; 
    }

    ptr = POS_TO_ADDRESS(handle, que_id);

    que->type = GET_TYPE_FROM_DATA(ptr);
    que->next_flag  = GET_FFLG_FROM_DATA(ptr);

    switch (que->type) {
    case QUE_TYPE_EMPTY:
    case QUE_TYPE_JIRI:
    case QUE_TYPE_FZK:
        return que;
    default:
        break;
    }
#ifdef LEARN_DEBUG
    printf("FATAL : Illegal que was gotten (que_id=%d)\n", que_id);
#endif 
    return NULL; 
}

static NJ_WQUE *get_que_yomiLen_and_hyoukiLen(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle,
                                              uint16_t que_id) {
    uint8_t *ptr;
    NJ_WQUE *que = &(iwnn->que_tmp);


    if (que_id >= GET_LEARN_MAX_WORD_COUNT(handle)) {
        return NULL; 
    }

    ptr = POS_TO_ADDRESS(handle, que_id);

    que->type        = GET_TYPE_FROM_DATA(ptr);
    que->yomi_byte   = GET_YSIZE_FROM_DATA(ptr);
    que->yomi_len    = que->yomi_byte / sizeof(NJ_CHAR);
    que->hyouki_byte = GET_KSIZE_FROM_DATA(ptr);
    que->hyouki_len  = que->hyouki_byte / sizeof(NJ_CHAR);

    switch (que->type) {
    case QUE_TYPE_JIRI:
    case QUE_TYPE_FZK:
        return que;
    default:
        break;
    }
#ifdef LEARN_DEBUG
    printf("FATAL : Illegal que was gotten (que_id=%d)\n", que_id);
#endif 
    return NULL;
}

static NJ_WQUE *get_que_allHinsi(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle,
                                 uint16_t que_id) {
    uint8_t *ptr;
    NJ_WQUE *que = &(iwnn->que_tmp);


    if (que_id >= GET_LEARN_MAX_WORD_COUNT(handle)) {
        return NULL; 
    }

    ptr = POS_TO_ADDRESS(handle, que_id);

    que->type      = GET_TYPE_FROM_DATA(ptr);
    que->mae_hinsi = GET_FPOS_FROM_DATA(ptr);
    que->ato_hinsi = GET_BPOS_FROM_DATA(ptr);

    switch (que->type) {
    case QUE_TYPE_JIRI:
    case QUE_TYPE_FZK:
        return que;
    default:
        break;
    }
#ifdef LEARN_DEBUG
    printf("FATAL : Illegal que was gotten (que_id=%d)\n", que_id);
#endif 
    return NULL; 
}

static NJ_WQUE *get_que(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id) {
    uint8_t *ptr;
    NJ_WQUE *que = &(iwnn->que_tmp);


    if (que_id >= GET_LEARN_MAX_WORD_COUNT(handle)) {
        return NULL; 
    }

    ptr = POS_TO_ADDRESS(handle, que_id);

    que->entry      = que_id;
    que->type       = GET_TYPE_FROM_DATA(ptr);
    que->mae_hinsi  = GET_FPOS_FROM_DATA(ptr);
    que->ato_hinsi  = GET_BPOS_FROM_DATA(ptr);
    que->yomi_byte  = GET_YSIZE_FROM_DATA(ptr);
    que->yomi_len   = que->yomi_byte / sizeof(NJ_CHAR);
    que->hyouki_byte= GET_KSIZE_FROM_DATA(ptr);
    que->hyouki_len = que->hyouki_byte / sizeof(NJ_CHAR);
    que->next_flag  = GET_FFLG_FROM_DATA(ptr);

    switch (que->type) {
    case QUE_TYPE_JIRI:
    case QUE_TYPE_FZK:
        return que;
    default:
        break;
    }
#ifdef LEARN_DEBUG
    printf("FATAL : Illegal que was gotten (que_id=%d)\n", que_id);
#endif 
    return NULL; 
}

static int16_t is_continued(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id) {
    NJ_WQUE *que;
    uint16_t i;
    uint16_t max, end;


    max = GET_LEARN_MAX_WORD_COUNT(handle);             
    end = GET_LEARN_NEXT_WORD_POS(handle);
    
    for (i = 0; i < max; i++) {
        que_id++;
        if (que_id >= GET_LEARN_MAX_WORD_COUNT(handle)) {
            
            que_id = 0;
        }

        
        if (que_id == end) {
            
            return 0;
        }

        que = get_que_type_and_next(iwnn, handle, que_id);
#ifdef IWNN_ERR_CHECK
        if (iwnn->err_check_flg == 1) {
            que = NULL;
        }
#endif 
        if (que == NULL) {
            return NJ_SET_ERR_VAL(NJ_FUNC_IS_CONTINUED, NJ_ERR_DIC_BROKEN);
        }
        if (que->type != QUE_TYPE_EMPTY) {
            
            if (que->next_flag != 0) {
                
                return 1;
            } else {
                
                return 0;
            }
        }
    }

    
    return 0; 
}

static int16_t continue_cnt(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint16_t que_id) {
    NJ_WQUE *que;
    uint16_t i;
    uint16_t max, end;
    int16_t cnt = 0;


    max = GET_LEARN_MAX_WORD_COUNT(handle);             
    end = GET_LEARN_NEXT_WORD_POS(handle);
    
    for (i = 0; i < max; i++) {
        que_id++;
        if (que_id >= max) {
            
            que_id = 0;
        }

        
        if (que_id == end) {
            
            return cnt;
        }

        que = get_que_type_and_next(iwnn, handle, que_id);
        if (que == NULL) {
            return NJ_SET_ERR_VAL(NJ_FUNC_CONTINUE_CNT, NJ_ERR_DIC_BROKEN); 
        }
        if (que->type != QUE_TYPE_EMPTY) {
            
            if (que->next_flag != 0) {
                
                cnt++;
                
                
                if (cnt >= (NJD_MAX_CONNECT_CNT - 1)) {
                    return cnt;
                }
            } else {
                
                return cnt;
            }
        }
    }

    
    return 0; 
}

static uint16_t search_next_que(NJ_DIC_HANDLE handle, uint16_t que_id) {
    uint16_t max;
    uint16_t i;


    max = GET_LEARN_MAX_WORD_COUNT(handle);             
    
    for (i = 0; i < max; i++) {
        que_id++;
        if (que_id >= max) {
            
            que_id = 0;
        }

        if (GET_TYPE_FROM_DATA(POS_TO_ADDRESS(handle, que_id)) != QUE_TYPE_EMPTY) {
            
            return que_id;
        }
    }

    
    return 0; 
}

static int16_t que_strcmp_complete_with_hyouki(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle,
                                                uint16_t que_id, NJ_CHAR *yomi, uint16_t yomi_len, NJ_CHAR *hyouki,
                                                uint8_t multi_flg) {
    NJ_CHAR *str;
    int16_t ret;
    uint8_t slen;
    uint16_t hyouki_len;
    uint16_t que_yomilen, que_hyoukilen;
    int16_t que_count = 1;
    int16_t cnt = 0;


    
    hyouki_len = nj_strlen(hyouki);

    if (multi_flg == 0) {
        
        cnt = 1;
    } else {
        
        
        cnt = GET_LEARN_WORD_COUNT(handle);
    }

    while (cnt--) {
        str = get_string(iwnn, handle, que_id, &slen);
        if (str == NULL) {
            return NJ_SET_ERR_VAL(NJ_FUNC_QUE_STRCMP_COMPLETE_WITH_HYOUKI, 
                                  NJ_ERR_DIC_BROKEN);
        }
        que_yomilen = slen;
        
        ret = nj_strncmp(yomi, str, que_yomilen);
        if (ret != 0) {
            
            return 0;
        }

        str = get_hyouki(iwnn, handle, que_id, &slen);
        if (str == NULL) {
            return NJ_SET_ERR_VAL(NJ_FUNC_QUE_STRCMP_COMPLETE_WITH_HYOUKI, 
                                  NJ_ERR_DIC_BROKEN);
        }
        que_hyoukilen = slen;
        
        ret = nj_strncmp(hyouki, str, que_hyoukilen);
        if (ret != 0) {
            
            return 0;
        }
        
        if ((yomi_len == que_yomilen) &&
            (hyouki_len == que_hyoukilen)) {
            
            return que_count;
        }
        
        if ((que_yomilen > yomi_len) ||
            (que_hyoukilen > hyouki_len)) {
            
            return 0; 
        }
        
        ret = is_continued(iwnn, handle, que_id);
        if (ret <= 0) {
            
            return ret;
        }
        
        
        if (que_count >= (NJD_MAX_CONNECT_CNT - 1)) {
            
            return 0;
        }
        
        yomi_len -= que_yomilen;
        yomi     += que_yomilen;

        hyouki_len -= que_hyoukilen;
        hyouki     += que_hyoukilen;

        
        que_id = search_next_que(handle, que_id);
        que_count++;
    }
    return 0; 
}

static int16_t que_strcmp_include(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle,
                                   uint16_t que_id, NJ_CHAR *yomi) {
    NJ_CHAR *str;
    uint16_t que_len;
    uint16_t yomi_len;
    int16_t ret;
    int16_t que_count = 1;
    uint16_t i = 0;
    uint8_t slen;


#ifdef LEARN_DEBUG
    printf("que_strcmp_include(que_id=%d, yomi=[%s])\n", que_id, yomi);
#endif 
    yomi_len = nj_strlen(yomi);
    if (yomi_len == 0) {
        return que_count;
    }
    
    i = GET_LEARN_WORD_COUNT(handle);

    while (--i) {        

        
        ret = is_continued(iwnn, handle, que_id);
        if (ret < 0) {
            
            return ret;
        } else if (ret == 0) {
            
            return que_count;
        }

        
        que_id = search_next_que(handle, que_id);

        str = get_string(iwnn, handle, que_id, &slen);
#ifdef IWNN_ERR_CHECK
        if (iwnn->err_check_flg == 2) {
            str = NULL;
        }
#endif 
        if (str == NULL) {
            return NJ_SET_ERR_VAL(NJ_FUNC_QUE_STRCMP_INCLUDE, NJ_ERR_DIC_BROKEN);
        }
        que_len = slen;

        
        if (que_len > yomi_len) {
#ifdef LEARN_DEBUG
            printf("  >> mismatch [%s] (que_len > yomi_len)\n", str);
#endif 
            return que_count;
        }

        
        ret = nj_strncmp(yomi, str, que_len);
        if (ret != 0) {
#ifdef LEARN_DEBUG
            printf("  >> mismatch [%s]\n", str);
#endif 
            
            return que_count;
        }

        
        if (que_len == yomi_len) {
#ifdef LEARN_DEBUG
            printf("  >> match! [%s](%d)\n", str, que_count);
#endif 
            return (que_count + 1);
        }

        que_count++;
        if (que_count >= NJD_MAX_CONNECT_CNT) {
            
            return que_count;
        }

        
        yomi_len -= que_len;
        yomi     += que_len;
    }

    return que_count;
}

static NJ_CHAR *get_string(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle,
                           uint16_t que_id, uint8_t *slen) {
    uint8_t *src, *dst;
    uint8_t copy_size, size;
    uint8_t i;
    uint8_t *top_addr;
    uint8_t *bottom_addr;
    uint16_t que_size;


    src = POS_TO_ADDRESS(handle, que_id);
    switch (GET_TYPE_FROM_DATA(src)) {
    case QUE_TYPE_JIRI:
    case QUE_TYPE_FZK:
        size =  GET_YSIZE_FROM_DATA(src);
        *slen = (uint8_t)(size / sizeof(NJ_CHAR));
        break;

    default:
#ifdef LEARN_DEBUG
        printf("get_string(handle=%p, que_id=%d) : broken que\n", handle, que_id);
#endif 
        return NULL;
    }
    
    if (NJ_GET_DIC_TYPE(handle) == NJ_DIC_TYPE_USER) {
        if (*slen > NJ_MAX_USER_LEN) {
            return NULL; 
        }
    } else {
        if (*slen > NJ_MAX_LEN) {
            return NULL;
        }
    }

    
    src += LEARN_QUE_STRING_OFFSET;

    que_size = QUE_SIZE(handle);

    
    copy_size = (uint8_t)que_size - LEARN_QUE_STRING_OFFSET;
    dst = (uint8_t*)&(iwnn->learn_string_tmp[0]);
    if (copy_size > size) {
        
        copy_size = size;
    }
    for (i = 0; i < copy_size; i++) {
        *dst++ = *src++;
    }

    
    top_addr = LEARN_DATA_TOP_ADDR(handle);
    bottom_addr = top_addr;
    bottom_addr += que_size * GET_LEARN_MAX_WORD_COUNT(handle) - 1;

    while (size -= copy_size) {

        if (src >= bottom_addr) {
            src = top_addr;
        }

        
        if (*src != QUE_TYPE_NEXT) {
#ifdef LEARN_DEBUG
            printf("FATAL: src que was broken(not QUE_TYPE_NEXT) [src=%x]\n", src);
#endif 
            return NULL;        
        }

        src++;  
        if (size < que_size) {
            
            copy_size = size;
        } else {
            copy_size = (uint8_t)(que_size - 1);
        }
        for (i = 0; i < copy_size; i++) {
            *dst++ = *src++;
        }
    }
    iwnn->learn_string_tmp[*slen] = NJ_CHAR_NUL;

    return &(iwnn->learn_string_tmp[0]);
}

static NJ_CHAR *get_hyouki(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle,
                           uint16_t que_id, uint8_t *slen) {
    uint8_t *src, *dst;
    NJ_WQUE *que;
    uint8_t copy_size, size;
    uint8_t i;
    uint8_t *top_addr;
    uint8_t *bottom_addr;
    NJ_CHAR  *hira;
    uint16_t que_size;
    uint32_t dictype;


    que = get_que_yomiLen_and_hyoukiLen(iwnn, handle, que_id);
    if (que == NULL) {
        return NULL;
    }
    
    dictype = NJ_GET_DIC_TYPE(handle);
    if (dictype == NJ_DIC_TYPE_USER) {
        if (que->yomi_len > NJ_MAX_USER_LEN) {
            return NULL; 
        }
        if (que->hyouki_len > NJ_MAX_USER_KOUHO_LEN) {
            return NULL; 
        }
    } else {
        if (que->yomi_len > NJ_MAX_LEN) {
            return NULL; 
        }
        if (que->hyouki_len > NJ_MAX_RESULT_LEN) {
            return NULL;
        }
    }

    src = POS_TO_ADDRESS(handle, que_id);
    
    if (que->hyouki_len == 0) {
        hira = get_string(iwnn, handle, que_id, slen);
        if (hira == NULL) {
            return NULL; 
        }
        
        if (GET_MFLG_FROM_DATA(src) != 0) {
            *slen = (uint8_t)nje_convert_hira_to_kata(hira, &(iwnn->muhenkan_tmp[0]), *slen);
            return &(iwnn->muhenkan_tmp[0]);
        } else {
            return hira;
        }
    }
    
    src += LEARN_QUE_STRING_OFFSET;

    que_size = QUE_SIZE(handle);

    
    size = que->yomi_byte;
    copy_size = (uint8_t)que_size - LEARN_QUE_STRING_OFFSET;
    dst = (uint8_t*)&(iwnn->learn_string_tmp[0]);
    if (copy_size > size) {
        
        copy_size = size;
    }

    
    top_addr = LEARN_DATA_TOP_ADDR(handle);
    bottom_addr = top_addr;
    bottom_addr += que_size * GET_LEARN_MAX_WORD_COUNT(handle) - 1;

    src += copy_size;
    while (size -= copy_size) {

        
        if (src >= bottom_addr) {
            src = top_addr;
        }

        
        if (*src != QUE_TYPE_NEXT) {
#ifdef LEARN_DEBUG
            printf("FATAL: src que was broken(not QUE_TYPE_NEXT) [src=%x]\n", src);
#endif 
            return NULL;        
        }

        src++;  
        if (size < que_size) {
            
            copy_size = size;
        } else {
            copy_size = (uint8_t)(que_size - 1);
        }
        src += copy_size;
    }


    
    if (((src - top_addr) % que_size) == 0) {

        if (src >= bottom_addr) {
            src = top_addr;
        }

        if (*src++ != QUE_TYPE_NEXT) {
#ifdef LEARN_DEBUG
            printf("FATAL: src que was broken(QUE_TYPE_NEXT) [src=%x]\n", src - 1);
#endif 
            return NULL; 
        }
    }

    size = que->hyouki_byte;

    
    copy_size = (uint8_t)(que_size);
    copy_size -= (uint8_t)((src - top_addr) % que_size);
    if (copy_size > size) {
        
        copy_size = size;
    }
    for (i = 0; i < copy_size; i++) {
        *dst++ = *src++;
    }

    while (size -= copy_size) {

        
        if (src >= bottom_addr) {
            src = top_addr;
        }

        
        if (*src != QUE_TYPE_NEXT) {
#ifdef LEARN_DEBUG
            printf("FATAL: src que was broken(not QUE_TYPE_NEXT) [src=%x]\n", src);
#endif 
            return NULL;        
        }

        src++;  
        if (size < que_size) {
            
            copy_size = size;
        } else {
            copy_size = (uint8_t)(que_size - 1);
        }

        for (i = 0; i < copy_size; i++) {
            *dst++ = *src++;
        }
    }

    *slen = que->hyouki_len;
    iwnn->learn_string_tmp[*slen] = NJ_CHAR_NUL;

    return &(iwnn->learn_string_tmp[0]);
}

static int16_t get_cand_by_sequential(NJ_CLASS *iwnn, NJ_SEARCH_CONDITION *cond,
                                       NJ_SEARCH_LOCATION_SET *loctset, uint8_t search_pattern,
                                       uint8_t comp_flg) {
    uint16_t current, from, to;
    uint16_t que_id;
    uint8_t  *ptr, *p;
    int16_t ret, num_count;
    NJ_CHAR  *yomi;
    NJ_WQUE  *que;
    uint8_t forward_flag = 0;


    
    if (GET_LOCATION_STATUS(loctset->loct.status) == NJ_ST_SEARCH_NO_INIT) {
        
        ret = search_range_by_yomi(iwnn, loctset->loct.handle, search_pattern,
                                   cond->yomi, cond->ylen, &from, &to, &forward_flag);
        if (ret < 0) {
            return ret;
        }
        if (ret == 0) {
            if (forward_flag) {
                loctset->loct.status = NJ_ST_SEARCH_END;
            } else {
                loctset->loct.status = NJ_ST_SEARCH_END_EXT;
            }
            return 0;
        }
        loctset->loct.top = from;
        loctset->loct.bottom = to;
        current = from;
    } else if (GET_LOCATION_STATUS(loctset->loct.status) == NJ_ST_SEARCH_READY) {
        
        current = (uint16_t)(loctset->loct.current + 1);
    } else {
        loctset->loct.status = NJ_ST_SEARCH_END; 
        return 0; 
    }

    
    ptr = get_search_index_address(loctset->loct.handle, cond->operation);
    p = ptr + (current * NJ_INDEX_SIZE);

    while (current <= loctset->loct.bottom) {
        que_id = GET_UINT16(p);
        if (search_pattern == NJ_CUR_OP_COMP) {
            
            ret = str_que_cmp(iwnn, loctset->loct.handle, cond->yomi, cond->ylen, que_id, 1);
            
            
            if (ret == 2) {
                ret = 0; 
            }
        } else if (search_pattern == NJ_CUR_OP_FORE) {
            
            ret = str_que_cmp(iwnn, loctset->loct.handle, cond->yomi, cond->ylen, que_id, 2);
            
            
            if (ret == 2) {
                ret = 0; 
            }
        } else {
            
            
            
            ret = que_strcmp_complete_with_hyouki(iwnn, loctset->loct.handle, que_id,
                                                  cond->yomi, cond->ylen, cond->kanji, 0);
        }
        
        if (ret < 0) {
            return ret;
        }
        if (ret > 0) {
            if (search_pattern == NJ_CUR_OP_LINK) {
                
                
                num_count = continue_cnt(iwnn, loctset->loct.handle, que_id);
                if (num_count < 0) {
                    
                    return num_count; 
                }
                
                
                if (num_count >= ret) {
                    
                    loctset->loct.current_info = (uint8_t)(((num_count + 1) << 4) | ret);
                    loctset->loct.current = current;
                    loctset->loct.status = NJ_ST_SEARCH_READY;
                    loctset->cache_freq = get_hindo(iwnn, loctset, search_pattern);
                    return 1;
                }
            } else {
                
                

                
                
                
                que = get_que_allHinsi(iwnn, loctset->loct.handle, que_id);
                if (njd_connect_test(cond, que->mae_hinsi, que->ato_hinsi)) {

                    
                    switch (NJ_GET_DIC_TYPE_EX(loctset->loct.type, loctset->loct.handle)) {
                    case NJ_DIC_TYPE_CUSTOM_INCOMPRESS:
                        if ((search_pattern == NJ_CUR_OP_COMP) && (comp_flg == 1)) {
                            yomi = cond->yomi + cond->ylen;
                            ret = que_strcmp_include(iwnn, loctset->loct.handle, que_id, yomi);
                            if (ret < 0) {
                                return ret;
                            }
                        }
                        break;
                    default:
                        break;
                    }
                    loctset->loct.current = current;
                    loctset->loct.status = NJ_ST_SEARCH_READY;
                    
                    loctset->loct.current_info = (ret & 0x0f) << 4;
                    loctset->cache_freq = get_hindo(iwnn, loctset, search_pattern);
                    return 1;
                }
            }
        }
        p += NJ_INDEX_SIZE;
        current++;
    }

    
    loctset->loct.status = NJ_ST_SEARCH_END;
    return 0;
}

static int16_t get_cand_by_evaluate(NJ_CLASS *iwnn, NJ_SEARCH_CONDITION *cond,
                                     NJ_SEARCH_LOCATION_SET *loctset, uint8_t search_pattern) {
    uint16_t from, to, i;
    uint16_t que_id, oldest;
    uint32_t max_value, eval, current;
    uint8_t  *ptr, *p;
    NJ_WQUE  *que;
    int16_t ret, num_count;
    int32_t found = 0;
    uint8_t forward_flag = 0;
    int32_t is_first_search, is_better_freq;


    
    ptr = get_search_index_address(loctset->loct.handle, cond->operation);

    
    oldest = GET_LEARN_NEXT_WORD_POS(loctset->loct.handle);

    
    current = 0;
    if (GET_LOCATION_STATUS(loctset->loct.status) == NJ_ST_SEARCH_NO_INIT) {
        if (search_pattern == NJ_CUR_OP_LINK) {
            
            
            
            ret = search_range_by_yomi_multi(iwnn, loctset->loct.handle, 
                                             cond->yomi, cond->ylen, &from, &to);
        } else {
            
            
            ret = search_range_by_yomi(iwnn, loctset->loct.handle, search_pattern,
                                       cond->yomi, cond->ylen, &from, &to, &forward_flag);
        }
        if (ret <= 0) {
            loctset->loct.status = NJ_ST_SEARCH_END;
            return ret;
        }
        loctset->loct.top = from;
        loctset->loct.bottom = to;
        is_first_search = 1;
    } else if (GET_LOCATION_STATUS(loctset->loct.status) == NJ_ST_SEARCH_READY) {
        current = GET_UINT16(ptr + (loctset->loct.current * NJ_INDEX_SIZE));
        if (current < oldest) {
            current += GET_LEARN_MAX_WORD_COUNT(loctset->loct.handle);
        }
        is_first_search = 0;
    } else {
        loctset->loct.status = NJ_ST_SEARCH_END; 
        return 0; 
    }

    
    max_value = oldest;

    p = ptr + (loctset->loct.top * NJ_INDEX_SIZE);
    eval = current;
    for (i = (uint16_t)loctset->loct.top; i <= (uint16_t)loctset->loct.bottom; i++) {
        que_id = GET_UINT16(p);
        if (que_id < oldest) {
            eval = que_id + GET_LEARN_MAX_WORD_COUNT(loctset->loct.handle);
        } else {
            eval = que_id;
        }
#ifdef LEARN_DEBUG
        printf("que(%d) : eval = %d\n", que_id, eval);
#endif 
        is_better_freq = ((eval >= max_value) && ((is_first_search) || (eval < current))) ? 1 : 0;

        if (is_better_freq) {
            
            if (search_pattern == NJ_CUR_OP_LINK) {
                
                ret = que_strcmp_complete_with_hyouki(iwnn, loctset->loct.handle, que_id,
                                                      cond->yomi, cond->ylen, cond->kanji, 1);
            } else {
                
                ret = str_que_cmp(iwnn, loctset->loct.handle, cond->yomi, cond->ylen, que_id, 2);
                
                if (ret == 2) {
                    ret = 0; 
                }
            }
            if (ret < 0) {
                return ret; 
            }
            if (ret >= 1) {
                if (search_pattern == NJ_CUR_OP_LINK) {
                    
                    
                    num_count = continue_cnt(iwnn, loctset->loct.handle, que_id);
                    if (num_count < 0) {
                        
                        return num_count; 
                    }
                    
                    
                    if (num_count >= ret) {
                        
                        loctset->loct.current_info = (uint8_t)(((num_count + 1) << 4) | ret);
                        loctset->loct.current = i;
                        max_value = eval;
                        found = 1;
                    }
                } else {
                    
                    
                    
                    
                    
                    que = get_que_allHinsi(iwnn, loctset->loct.handle, que_id);
                    if (njd_connect_test(cond, que->mae_hinsi, que->ato_hinsi)) {
                        
                        loctset->loct.current_info = (uint8_t)0x10;
                        loctset->loct.current = i;
                        max_value = eval;
                        found = 1;
#ifdef LEARN_DEBUG
                        printf("---keep.");
#endif 
                    }
                }
            }
        }
        p += NJ_INDEX_SIZE;
    }

    
    if (found == 0) {
        loctset->loct.status = NJ_ST_SEARCH_END;
        return 0;
    } else {
        loctset->loct.status = NJ_ST_SEARCH_READY;
        loctset->cache_freq = get_hindo(iwnn, loctset, search_pattern);
        return 1;
    }

}

static int16_t search_range_by_yomi(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint8_t op,
                                     NJ_CHAR  *yomi, uint16_t len, uint16_t *from, uint16_t *to,
                                     uint8_t *forward_flag) {
    uint16_t right, mid = 0, left, max;
    uint16_t que_id;
    uint8_t  *ptr, *p;
    NJ_CHAR  *str;
    int16_t ret = 0;
    int32_t found = 0;
    uint8_t slen;
    int32_t cmp;


    
    ptr = get_search_index_address(handle, op);
    
    max = GET_LEARN_WORD_COUNT(handle);

    right = max - 1;
    left = 0;

#ifdef LEARN_DEBUG
    printf("src:[%s]\n", yomi);
#endif 

    *forward_flag = 0;

    
    switch (op) {
    case NJ_CUR_OP_COMP:
    case NJ_CUR_OP_LINK:
    case NJ_CUR_OP_FORE:
        
        
        
        break;
    default:
        return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_PARAM_OPERATION); 
    }

    while (left <= right) {
        mid = left + ((right - left) / 2);
        p = ptr + (mid * NJ_INDEX_SIZE);
        que_id = GET_UINT16(p);
        str = get_string(iwnn, handle, que_id, &slen);

#ifdef IWNN_ERR_CHECK
        if (iwnn->err_check_flg == 3) {
            str = NULL;
        }
#endif 
        if (str == NULL) {
            return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_DIC_BROKEN);
        }

        ret = nj_strncmp(yomi, str, len);
        if (op != NJ_CUR_OP_FORE) {
            
            
            if (ret == 0) {
                if ((*forward_flag == 0) && (len <= (uint16_t)slen)) {
                    
                    *forward_flag = 1;
                }
                if (len > (uint16_t)slen) {
                    ret = 1;
                } else if (len < (uint16_t)slen) {
                    ret = -1;
                }
            }
        }
#ifdef LEARN_DEBUG
        printf("   [%d][%d][%d]COMPARE:[%s] = %d\n", left, mid, right, str, ret);
#endif 
        if (ret == 0) {
            
            found = 1;
            break;
        } else if (ret < 0) {
            
            right = mid - 1;
            if (mid == 0) {
                break;
            }
        } else {
            
            left = mid + 1;
        }
    }

    if (!found) {
        return 0;
    }

    if (mid == 0) {
        *from = mid;
    } else {
        
        p = ((mid - 1) * NJ_INDEX_SIZE) + ptr;
        
        for (cmp = mid - 1; cmp >= 0; cmp--) {
            que_id = GET_UINT16(p);
            str = get_string(iwnn, handle, que_id, &slen);

#ifdef IWNN_ERR_CHECK
            if (iwnn->err_check_flg == 4) {
                str = NULL;
            }
#endif 
            if (str == NULL) {
                return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_DIC_BROKEN);
            }

            if (op != NJ_CUR_OP_FORE) {
                ret = nj_strncmp(yomi, str, len);
                if (ret == 0) {
                    if (len > (uint16_t)slen) {
                        ret = 1;
                    } else if (len < (uint16_t)slen) {
                        ret = -1;
                    }
                }
                if (ret > 0) {
                    
                    break;
                }
            } else {
                
                if (nj_strncmp(yomi, str, len) != 0) {
                    break;      
                }
            }
            p -= NJ_INDEX_SIZE;
        }
        if (cmp < 0) {
            *from = 0;
        } else {
            *from = (uint16_t)cmp + 1;
        }
    }

#ifdef LEARN_DEBUG
    printf("  >> from:(%d)\n", *from);
#endif 

#ifdef IWNN_ERR_CHECK
    if (iwnn->err_check_flg == 5) {
        mid = max - 2;
    }
#endif 
    if ((mid + 1) >= max) {
        *to = mid;
    } else {
        
        p = ((mid + 1) * NJ_INDEX_SIZE) + ptr;
        
        for (right = mid + 1; right < max; right++) {
            que_id = GET_UINT16(p);
            str = get_string(iwnn, handle, que_id, &slen);

#ifdef IWNN_ERR_CHECK
            if (iwnn->err_check_flg == 5) {
                str = NULL;
            }
#endif 
            if (str == NULL) {
                return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_DIC_BROKEN);
            }

            if (op != NJ_CUR_OP_FORE) {
                ret = nj_strncmp(yomi, str, len);
                if (ret == 0) {
                    if (len > (uint16_t)slen) {
                        ret = 1;
                    } else if (len < (uint16_t)slen) {
                        ret = -1;
                    }
                }
                if (ret < 0) {
                    
                    break;
                }
            } else {
                
                if (nj_strncmp(yomi, str, len) != 0) {
                    break;      
                }
            }
            p += NJ_INDEX_SIZE;
        }
        *to = right - 1;
    }

#ifdef LEARN_DEBUG
    printf("  >> to:(%d)\n", *to);
#endif 
    return 1;
}

static int16_t search_range_by_yomi_multi(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle,
                                           NJ_CHAR *yomi, uint16_t len, uint16_t *from, uint16_t *to) {
    uint16_t right, mid = 0, left, max = 0;
    uint16_t que_id;
    uint8_t  *ptr, *p;
    int16_t ret = 0;
    uint16_t comp_len;
    uint16_t i, char_len;
    int32_t found = 0;
    int32_t cmp;
    NJ_CHAR  comp_yomi[NJ_MAX_LEN + NJ_TERM_LEN];
    NJ_CHAR  *pYomi;


    
    
    ptr = LEARN_INDEX_TOP_ADDR(handle);

    
    max = GET_LEARN_WORD_COUNT(handle);

#ifdef LEARN_DEBUG
    printf("src:[%s]\n", yomi);
#endif 

    comp_len = 0;
    pYomi = &yomi[0];
    while (comp_len < len) {
        
        
        char_len = NJ_CHAR_LEN(pYomi);
        for (i = 0; i < char_len; i++) {
            *(comp_yomi + comp_len) = *pYomi;
            comp_len++;
            pYomi++;
        }
        *(comp_yomi + comp_len) = NJ_CHAR_NUL;

        right = max - 1;
        left = 0;
        while (left <= right) {
            mid = left + ((right - left) / 2);
            p = ptr + (mid * NJ_INDEX_SIZE);
            que_id = GET_UINT16(p);

            
            ret = str_que_cmp(iwnn, handle, comp_yomi, comp_len, que_id, 1);
            if (ret < 0) {
                return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI_MULTI, NJ_ERR_DIC_BROKEN); 
            }
    
#ifdef LEARN_DEBUG
            printf("   [%d][%d][%d]COMPARE:[%s] = %d\n", left, mid, right, str, ret);
#endif 
            if (ret == 1) {
                
                found = 1;
                break;
            } else if (ret == 0) {
                
                right = mid - 1;
                if (mid == 0) {
                    break;
                }
            } else {
                
                left = mid + 1;
            }
        }
        
        if (found) {
            break;
        }
    }

    if (!found) {
        
        return 0;
    }

    
    if (mid == 0) {
        *from = mid;
    } else {
        
        p = ((mid - 1) * NJ_INDEX_SIZE) + ptr;
        
        for (cmp = mid - 1; cmp >= 0; cmp--) {
            que_id = GET_UINT16(p);
            ret = str_que_cmp(iwnn, handle, comp_yomi, comp_len, que_id, 1);
            if (ret < 0) {
                return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI_MULTI, NJ_ERR_DIC_BROKEN); 
            }
            if (ret == 2) {
                break;
            }
            p -= NJ_INDEX_SIZE;
        }
        if (cmp < 0) {
            *from = 0;
        } else {
            *from = (uint16_t)cmp + 1;
        }
    }

#ifdef LEARN_DEBUG
    printf("  >> from:(%d)\n", *from);
#endif 

    
    if ((mid + 1) >= max) {
        *to = mid;
    } else {
        
        p = ((mid + 1) * NJ_INDEX_SIZE) + ptr;
        
        for (right = mid + 1; right < max; right++) {
            que_id = GET_UINT16(p);
            ret = str_que_cmp(iwnn, handle, yomi, len, que_id, 1);
            if (ret < 0) {
                return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI_MULTI, NJ_ERR_DIC_BROKEN); 
            }
            if (ret == 0) {
                break;
            }
            p += NJ_INDEX_SIZE;
        }
        *to = right - 1;
    }

#ifdef LEARN_DEBUG
    printf("  >> to:(%d)\n", *to);
#endif 
    return 1;
}

static int16_t str_que_cmp(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, NJ_CHAR *yomi,
                            uint16_t yomiLen, uint16_t que_id, uint8_t mode) {
    NJ_CHAR   *queYomi;
    uint8_t  queYomiLen;
    uint16_t i;


#ifdef IWNN_ERR_CHECK
    if (iwnn->err_check_flg == 6) {
        que_id = GET_LEARN_MAX_WORD_COUNT(handle);
    }
#endif 
    if (que_id >= GET_LEARN_MAX_WORD_COUNT(handle)) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_STR_QUE_CMP, NJ_ERR_DIC_BROKEN);
    }

    queYomi = get_string(iwnn, handle, que_id, &queYomiLen);
#ifdef IWNN_ERR_CHECK
    if (iwnn->err_check_flg == 7) {
        return NJ_SET_ERR_VAL(NJ_FUNC_STR_QUE_CMP, NJ_ERR_DIC_BROKEN);
    }
#endif 
    if (queYomi == NULL) {
        return NJ_SET_ERR_VAL(NJ_FUNC_STR_QUE_CMP, NJ_ERR_DIC_BROKEN);
    }

    
    if ((mode == 2) && (yomiLen == 0)) {
        return 1;
    }

    for (i = 0; i < yomiLen && i < queYomiLen; i++) {
        if (yomi[i] < queYomi[i]) {
            return 0;
        }
        if (yomi[i] > queYomi[i]) {
            return 2;
        }
    }

    if (yomiLen == queYomiLen) {
        return 1;
    }
    if (yomiLen < queYomiLen) {
        return (mode == 2) ? 1 : 0;
    }
    return (mode == 2) ? 2 : (mode + 1);
}

static NJ_HINDO calculate_hindo(NJ_DIC_HANDLE handle, int32_t freq, NJ_DIC_FREQ *dic_freq, int16_t freq_max, int16_t freq_min) {
    uint16_t max;
    NJ_HINDO  hindo;


    max = GET_LEARN_MAX_WORD_COUNT(handle);

    
    
    
    if (NJ_GET_DIC_TYPE(handle) == NJ_DIC_TYPE_USER) {
        
        hindo = (int16_t)dic_freq->base;
    } else {
        
        if (max > 1) {
            
            hindo = CALCULATE_HINDO(freq, dic_freq->base, dic_freq->high, (max-1));
        } else {
            
            hindo = (int16_t)dic_freq->high;
        }
    }
    return NORMALIZE_HINDO(hindo, freq_max, freq_min);
}

static NJ_HINDO get_hindo(NJ_CLASS *iwnn, NJ_SEARCH_LOCATION_SET *loctset,
                          uint8_t search_pattern) {
    NJ_WQUE   *que;
    uint16_t que_id, oldest;
    uint8_t  offset;
    int32_t  dic_freq;
    uint16_t max;
    uint8_t  *learn_index_top_addr;


    
    learn_index_top_addr = get_search_index_address(loctset->loct.handle, search_pattern);

    que_id = (uint16_t)GET_UINT16(learn_index_top_addr +
                                   ((loctset->loct.current & 0xffffU) * NJ_INDEX_SIZE));
    oldest = GET_LEARN_NEXT_WORD_POS(loctset->loct.handle);

    offset = (loctset->loct.current_info & 0x0f);
    while (offset--) {
        que_id = search_next_que(loctset->loct.handle, que_id);
    }

    que = get_que(iwnn, loctset->loct.handle, que_id);
    if (que == NULL) {
        return INIT_HINDO; 
    }

    max = GET_LEARN_MAX_WORD_COUNT(loctset->loct.handle);
    if (que_id >= oldest) {
        dic_freq = que_id - oldest;
    } else {
        dic_freq = que_id - oldest + max;
    }

    
    return calculate_hindo(loctset->loct.handle, dic_freq, &(loctset->dic_freq), 1000, 0);
}

int16_t njd_l_get_word(NJ_CLASS *iwnn, NJ_SEARCH_LOCATION_SET *loctset, NJ_WORD *word) {
    NJ_WQUE *que;
    uint16_t que_id;
    uint8_t offset;
    uint8_t *learn_index_top_addr;


    
    learn_index_top_addr = get_search_index_address(loctset->loct.handle, GET_LOCATION_OPERATION(loctset->loct.status));

    que_id = (uint16_t)GET_UINT16(learn_index_top_addr +
                                   ((loctset->loct.current & 0xffff) * NJ_INDEX_SIZE));

    offset = (loctset->loct.current_info & 0x0f);
    while (offset--) {
        que_id = search_next_que(loctset->loct.handle, que_id);
    }

    que = get_que(iwnn, loctset->loct.handle, que_id);
    if (que == NULL) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_GET_WORD, NJ_ERR_CANNOT_GET_QUE); 
    }

    word->stem.loc = loctset->loct;

    word->stem.loc.current &= 0x0000ffff;
    word->stem.loc.current |= ((uint32_t)que_id << 16);
    
    
    word->stem.hindo = loctset->cache_freq;

    NJ_SET_FPOS_TO_STEM(word, que->mae_hinsi);
    NJ_SET_YLEN_TO_STEM(word, que->yomi_len);
    if (que->hyouki_len > 0) {
        NJ_SET_KLEN_TO_STEM(word, que->hyouki_len);
    } else {
        
        NJ_SET_KLEN_TO_STEM(word, que->yomi_len);
    }
    NJ_SET_BPOS_TO_STEM(word, que->ato_hinsi);

    
    word->stem.type = 0;

    return 1;
}

int16_t njd_l_get_stroke(NJ_CLASS *iwnn, NJ_WORD *word, NJ_CHAR *stroke, uint16_t size) {
    uint16_t que_id;
    NJ_CHAR   *str;
    uint8_t  slen;
    uint8_t  ylen;


    que_id = (uint16_t)(word->stem.loc.current >> 16);

    
    ylen = (uint8_t)NJ_GET_YLEN_FROM_STEM(word);

    if ((uint16_t)((ylen+ NJ_TERM_LEN)*sizeof(NJ_CHAR)) > size) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_GET_STROKE, NJ_ERR_BUFFER_NOT_ENOUGH);
    }
    if (ylen == 0) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_GET_STROKE, NJ_ERR_INVALID_RESULT); 
    }
    str = get_string(iwnn, word->stem.loc.handle, que_id, &slen);

#ifdef IWNN_ERR_CHECK
    if (iwnn->err_check_flg == 9) {
        str = NULL;
    }
#endif 
    
    if (str == NULL) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_GET_STROKE, NJ_ERR_DIC_BROKEN);
    }

    
    nj_strcpy(stroke, str);
    
    return slen;
}

int16_t njd_l_get_candidate(NJ_CLASS *iwnn, NJ_WORD *word,
                             NJ_CHAR *candidate, uint16_t size) {
    uint16_t que_id;
    NJ_CHAR   *str;
    uint16_t klen;
    uint8_t  slen;


    que_id = (uint16_t)(word->stem.loc.current >> 16);

    
    klen = NJ_GET_KLEN_FROM_STEM(word);

    if (size < ((klen+NJ_TERM_LEN)*sizeof(NJ_CHAR))) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_GET_CANDIDATE, NJ_ERR_BUFFER_NOT_ENOUGH);
    }
    str = get_hyouki(iwnn, word->stem.loc.handle, que_id, &slen);
#ifdef IWNN_ERR_CHECK
    if (iwnn->err_check_flg == 10) {
        str = NULL;
    }
#endif 
    if (str == NULL) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_GET_CANDIDATE, NJ_ERR_DIC_BROKEN);
    }

    
    nj_strcpy(candidate, str);

    return klen;
}

int16_t njd_l_check_dic(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle) {
    uint16_t flg;
    uint16_t word_cnt, max;
    uint8_t *ptr;
    uint16_t target_id;
    uint16_t i;
    uint16_t id1 = 0;
    uint8_t slen;


    
    if ((NJ_GET_DIC_TYPE(handle) != NJ_DIC_TYPE_USER)) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_CHECK_DIC, NJ_ERR_DIC_TYPE_INVALID); 
    }

    
    word_cnt = GET_LEARN_WORD_COUNT(handle);
    max = GET_LEARN_MAX_WORD_COUNT(handle);
    if (word_cnt > max) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_CHECK_DIC,
                              NJ_ERR_DIC_BROKEN);
    }
    
    ptr = LEARN_INDEX_TOP_ADDR(handle);
    for (i = 0; i < word_cnt; i++) {
        id1 = GET_UINT16(ptr);
        
        if (id1 >= max) {
            return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_CHECK_DIC, 
                                  NJ_ERR_DIC_BROKEN);
        }
        ptr += NJ_INDEX_SIZE;
    }

    
    ptr = LEARN_INDEX_TOP_ADDR2(handle);
    for (i = 0; i < word_cnt; i++) {
        id1 = GET_UINT16(ptr);
        
        if (id1 >= max) {
            return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_CHECK_DIC, 
                                  NJ_ERR_DIC_BROKEN);
        }
        ptr += NJ_INDEX_SIZE;
    }

    
    flg = GET_UINT16(handle + POS_WRITE_FLG);
    
    target_id = GET_UINT16(handle + POS_WRITE_FLG + 2);
    
    
    
    if (((flg != word_cnt) && (flg != (word_cnt + 1)) && (flg != (word_cnt - 1))) ||
        (target_id >= max)) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_CHECK_DIC,
                              NJ_ERR_DIC_BROKEN);
    }

    
    if (flg == (word_cnt + 1)) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_CHECK_DIC, NJ_ERR_DIC_BROKEN);
    } else if (flg == (word_cnt - 1)) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_CHECK_DIC, NJ_ERR_DIC_BROKEN);
    }

    word_cnt = GET_LEARN_WORD_COUNT(handle);

    ptr = LEARN_INDEX_TOP_ADDR(handle);
    for (i = 0; i < word_cnt; i++) {
        id1 = GET_UINT16(ptr);
        if (get_hyouki(iwnn, handle, id1, &slen) == NULL) {
            return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_CHECK_DIC,
                                  NJ_ERR_DIC_BROKEN);
        }
        ptr += NJ_INDEX_SIZE;
    }

    ptr = LEARN_INDEX_TOP_ADDR2(handle);
    for (i = 0; i < word_cnt; i++) {
        id1 = GET_UINT16(ptr);
        
        if (id1 >= max) {
            return NJ_SET_ERR_VAL(NJ_FUNC_NJD_L_CHECK_DIC,
                                  NJ_ERR_DIC_BROKEN);
        }
        ptr += NJ_INDEX_SIZE;
    }

    return 0;
}

static int16_t get_cand_by_evaluate2(NJ_CLASS *iwnn, NJ_SEARCH_CONDITION *cond,
                                      NJ_SEARCH_LOCATION_SET *loctset,
                                      uint8_t search_pattern,
                                      uint16_t idx) {
    uint16_t from, to, i;
    uint16_t que_id, oldest;
    uint32_t max_value, eval, current;
    uint8_t  *ptr, *p;
    NJ_WQUE *que;
    int16_t ret = 0;
    int32_t found = 0;
    uint8_t forward_flag = 0;


    uint16_t               abIdx;
    uint16_t               abIdx_old;
    uint16_t               tmp_len;
    uint16_t               yomi_clen;
    uint16_t               j,l,m;
    uint8_t                cmpflg;
    uint8_t                endflg = 0;
    NJ_CHAR                 *str;
    NJ_CHAR                 *key;
    NJ_CHAR                 char_tmp[NJ_MAX_LEN + NJ_TERM_LEN];
    NJ_CHAR                 *pchar_tmp;
    NJ_SEARCH_CACHE         *psrhCache = cond->ds->dic[idx].srhCache;
    uint16_t               endIdx;
    uint8_t                slen;
    uint16_t               addcnt = 0;
    NJ_CHAR                 *yomi;
    uint8_t                aimai_flg = 0x01;
    NJ_CHARSET              *pCharset = cond->charset;


    if (NJ_GET_CACHEOVER_FROM_SCACHE(psrhCache)) {
        aimai_flg = 0x00;
    }

    
    ptr = get_search_index_address(loctset->loct.handle, cond->operation);

    
    oldest = GET_LEARN_NEXT_WORD_POS(loctset->loct.handle);
    max_value = oldest;

    
    current = 0;
    if (GET_LOCATION_STATUS(loctset->loct.status) == NJ_ST_SEARCH_NO_INIT) {
        
        
        key       = cond->ds->keyword;
        yomi      = cond->yomi;
        yomi_clen = cond->yclen;
        
        
        endflg = 0x00;

        if (psrhCache->keyPtr[0] == 0xFFFF) {
            cmpflg = 0x01;
            psrhCache->keyPtr[0] = 0x0000;
        } else {
            cmpflg = 0x00;
        }

        for (i = 0; i < yomi_clen; i++) {
            j = i;
            
            
            if (!cmpflg) { 
                
                if (((j != 0) && (psrhCache->keyPtr[j] == 0)) || (psrhCache->keyPtr[j+1] == 0)) {
                    
                    cmpflg = 0x01;
                } else {
                    
                }
            }

            if (cmpflg) { 
                
                if (!j) { 
                    
                    abIdx = 0;
                    addcnt = 0;
                    nj_charncpy(char_tmp, yomi, 1);
                    tmp_len = nj_strlen(char_tmp);
                    ret = search_range_by_yomi(iwnn, loctset->loct.handle, search_pattern,
                                               char_tmp, tmp_len, &from,
                                               &to, &forward_flag);
                    if (ret < 0) {
                        
                        
                        psrhCache->keyPtr[j+1] = abIdx; 
                        loctset->loct.status = NJ_ST_SEARCH_END; 
                        return ret; 
                    } else if (ret > 0) {
                        
                        psrhCache->storebuff[abIdx].top    = from;
                        psrhCache->storebuff[abIdx].bottom = to;
                        psrhCache->storebuff[abIdx].idx_no = (int8_t)tmp_len;
                        addcnt++;
                        abIdx++;
                        psrhCache->keyPtr[j+1] = abIdx;
                    } else {
                        psrhCache->keyPtr[j+1] = abIdx;
                    }

                    if ((!endflg) && (pCharset != NULL) && aimai_flg) {
                        
                        for (l = 0; l < pCharset->charset_count; l++) {
                            
                            if (nj_charncmp(yomi, pCharset->from[l], 1) == 0) {
                                
                                nj_strcpy(char_tmp, pCharset->to[l]);
                                tmp_len = nj_strlen(char_tmp);
                                ret = search_range_by_yomi(iwnn, loctset->loct.handle, search_pattern,
                                                           char_tmp, tmp_len, &from, &to, &forward_flag);
                                if (ret < 0) {
                                    
                                    
                                    psrhCache->keyPtr[j+1] = abIdx; 
                                    loctset->loct.status = NJ_ST_SEARCH_END; 
                                    return ret; 
                                } else if (ret > 0) {
                                    
                                    
                                    if (abIdx >= NJ_SEARCH_CACHE_SIZE) {
                                        psrhCache->keyPtr[j+1] = 0;
                                        return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_CACHE_NOT_ENOUGH);
                                    }
                                    psrhCache->storebuff[abIdx].top    = from;
                                    psrhCache->storebuff[abIdx].bottom = to;
                                    psrhCache->storebuff[abIdx].idx_no = (int8_t)tmp_len;
                                    if (addcnt == 0) {
                                        psrhCache->keyPtr[j] = abIdx;
                                    }
                                    abIdx++;
                                    addcnt++;
                                    psrhCache->keyPtr[j+1] = abIdx;
                                } else {
                                    psrhCache->keyPtr[j+1] = abIdx;
                                }
                            } 
                        } 
                    } 
                } else {
                    
                    if (psrhCache->keyPtr[j] == psrhCache->keyPtr[j-1]) {
                        
                        psrhCache->keyPtr[j+1] = psrhCache->keyPtr[j-1];
                        endflg = 0x01;
                    } else {
                        
                        endIdx = psrhCache->keyPtr[j];
                        abIdx_old = psrhCache->keyPtr[j-1];

                        if (NJ_GET_CACHEOVER_FROM_SCACHE(psrhCache)) {
                            abIdx = psrhCache->keyPtr[j - 1];
                            psrhCache->keyPtr[j] = abIdx;
                        } else {
                            abIdx = psrhCache->keyPtr[j];
                        }
                        addcnt = 0;

                        if ((abIdx > NJ_SEARCH_CACHE_SIZE) || (abIdx_old >= NJ_SEARCH_CACHE_SIZE) ||
                            (endIdx > NJ_SEARCH_CACHE_SIZE)) {
                            
                            return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_CACHE_BROKEN); 
                        }
                        for (m = abIdx_old; m < endIdx; m++) {
                            
                            p = ptr + (psrhCache->storebuff[m].top * NJ_INDEX_SIZE);
                            que_id = GET_UINT16(p);

                            
                            str = get_string(iwnn, loctset->loct.handle, que_id, &slen);

                            if (str == NULL) {
                                return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_DIC_BROKEN); 
                            }
                            
                            
                            nj_strncpy(char_tmp, str, psrhCache->storebuff[m].idx_no);
                            char_tmp[psrhCache->storebuff[m].idx_no] = NJ_CHAR_NUL;
                            
                            pchar_tmp = &char_tmp[psrhCache->storebuff[m].idx_no];
                            nj_charncpy(pchar_tmp, yomi, 1);
                            tmp_len = nj_strlen(char_tmp);
                            
                            
                            ret = search_range_by_yomi2(iwnn, loctset->loct.handle, search_pattern,
                                                        char_tmp, tmp_len, 
                                                        (uint16_t)(psrhCache->storebuff[m].top),
                                                        (uint16_t)(psrhCache->storebuff[m].bottom),
                                                        &from, &to, &forward_flag);
                            if (ret < 0) {
                                
                                
                                psrhCache->keyPtr[j+1] = abIdx; 
                                loctset->loct.status = NJ_ST_SEARCH_END; 
                                return ret; 
                            } else if (ret > 0) {
                                
                                
                                if (abIdx >= NJ_SEARCH_CACHE_SIZE) {
                                    psrhCache->keyPtr[j+1] = 0;
                                    return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_CACHE_NOT_ENOUGH);
                                }
                                psrhCache->storebuff[abIdx].top    = from;
                                psrhCache->storebuff[abIdx].bottom = to;
                                psrhCache->storebuff[abIdx].idx_no = (int8_t)tmp_len;
                                if (addcnt == 0) {
                                    psrhCache->keyPtr[j] = abIdx;
                                }
                                abIdx++;
                                addcnt++;
                                psrhCache->keyPtr[j+1] = abIdx;
                            } else {
                                psrhCache->keyPtr[j+1] = abIdx;
                            }
                            
                            if ((!endflg) && (pCharset != NULL) && aimai_flg) {
                                
                                for (l = 0; l < pCharset->charset_count; l++) {
                                    
                                    if (nj_charncmp(yomi, pCharset->from[l], 1) == 0) {
                                        
                                        tmp_len = nj_strlen(pCharset->to[l]);

                                        nj_strncpy(pchar_tmp, pCharset->to[l], tmp_len);
                                        *(pchar_tmp + tmp_len) = NJ_CHAR_NUL;
                                        tmp_len = nj_strlen(char_tmp);
                                        ret = search_range_by_yomi2(iwnn, loctset->loct.handle, search_pattern,
                                                                    char_tmp, tmp_len,
                                                                    (uint16_t)(psrhCache->storebuff[m].top),
                                                                    (uint16_t)(psrhCache->storebuff[m].bottom),
                                                                    &from, &to, &forward_flag);
                                        if (ret < 0) {
                                            
                                            
                                            psrhCache->keyPtr[j+1] = abIdx; 
                                            loctset->loct.status = NJ_ST_SEARCH_END; 
                                            return ret; 
                                        } else if (ret > 0) {
                                            
                                            
                                            if (abIdx >= NJ_SEARCH_CACHE_SIZE) {
                                                psrhCache->keyPtr[j+1] = 0;
                                                return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_CACHE_NOT_ENOUGH);
                                            }
                                            psrhCache->storebuff[abIdx].top    = from;
                                            psrhCache->storebuff[abIdx].bottom = to;
                                            psrhCache->storebuff[abIdx].idx_no = (int8_t)tmp_len;
                                            abIdx++;
                                            addcnt++;
                                            psrhCache->keyPtr[j+1] = abIdx;
                                        } else {
                                            psrhCache->keyPtr[j+1] = abIdx;
                                        }
                                    } 
                                } 
                            } 
                        } 
                    } 
                }
            }
            yomi += UTL_CHAR(yomi);
            key  += UTL_CHAR(key);
        }

        
        if ((addcnt == 0) && (psrhCache->keyPtr[yomi_clen - 1] == psrhCache->keyPtr[yomi_clen])) {
            endflg = 0x01;
        }

        if (endflg) {           
            loctset->loct.status = NJ_ST_SEARCH_END;
            return 0;
        }
    } else if (GET_LOCATION_STATUS(loctset->loct.status) == NJ_ST_SEARCH_READY) {
        current = GET_UINT16(ptr + (loctset->loct.current * NJ_INDEX_SIZE));
        if (current < oldest) {
            current += GET_LEARN_MAX_WORD_COUNT(loctset->loct.handle);
        }
    } else {
        loctset->loct.status = NJ_ST_SEARCH_END; 
        return 0; 
    }

    
    j = cond->yclen - 1;

    abIdx = psrhCache->keyPtr[j];
    abIdx_old = psrhCache->keyPtr[j+1];
    
    endIdx = abIdx_old;
    if ((abIdx >= NJ_SEARCH_CACHE_SIZE) || (abIdx_old > NJ_SEARCH_CACHE_SIZE)) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_CACHE_BROKEN); 
    }
    p = ptr + (psrhCache->storebuff[abIdx].top * NJ_INDEX_SIZE);
    que_id = GET_UINT16(p);
    eval = current;

    

    if (psrhCache->keyPtr[j] < psrhCache->keyPtr[j + 1]) {
        if (GET_LOCATION_STATUS(loctset->loct.status) == NJ_ST_SEARCH_NO_INIT) {
            endIdx = abIdx + 1;
            NJ_SET_AIMAI_TO_SCACHE(psrhCache);
        }

        for (m = abIdx; m < endIdx; m++) {
            p = ptr + (psrhCache->storebuff[m].top * NJ_INDEX_SIZE);
            que_id = GET_UINT16(p);
            eval = current;

            for (i = (uint16_t)psrhCache->storebuff[m].top; i <= (uint16_t)psrhCache->storebuff[m].bottom; i++) {
                que_id = GET_UINT16(p);
                if (que_id < oldest) {
                    eval = que_id + GET_LEARN_MAX_WORD_COUNT(loctset->loct.handle);
                } else {
                    eval = que_id;
                }
#ifdef LEARN_DEBUG
                printf("que(%d) : eval = %d : %d\n", que_id, eval, i);
#endif         
                if (eval >= max_value) {
                    if ((GET_LOCATION_STATUS(loctset->loct.status) == NJ_ST_SEARCH_NO_INIT)
                        || ((GET_LOCATION_STATUS(loctset->loct.status) == NJ_ST_SEARCH_READY)
                            && (NJ_GET_AIMAI_FROM_SCACHE(psrhCache)))
                        || (eval < current)) {

                        
                        
                        str = get_string(iwnn, loctset->loct.handle, que_id, &slen);
                        if (str == NULL) {
                            return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_DIC_BROKEN); 
                        }

                        
                        
                        que = get_que_allHinsi(iwnn, loctset->loct.handle, que_id);
                        if (njd_connect_test(cond, que->mae_hinsi, que->ato_hinsi)) {
                            
                            loctset->loct.current_info = (uint8_t)0x10;
                            loctset->loct.current = i;
                            max_value = eval;
                            found = 1;
#ifdef LEARN_DEBUG
                            printf("---keep.");
#endif 
                        }
                    }
                }
                p += NJ_INDEX_SIZE;
            }
        }
    }

    if (GET_LOCATION_STATUS(loctset->loct.status) != NJ_ST_SEARCH_NO_INIT) {
        NJ_UNSET_AIMAI_TO_SCACHE(psrhCache);
    }

    
    if (found == 0) {
        loctset->loct.status = NJ_ST_SEARCH_END;
        return 0;
    } else {
        loctset->loct.status = NJ_ST_SEARCH_READY;
        loctset->cache_freq = get_hindo(iwnn, loctset, search_pattern);
        return 1;
    }
}

static int16_t search_range_by_yomi2(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint8_t op,
                                      NJ_CHAR  *yomi, uint16_t len,
                                      uint16_t sfrom, uint16_t sto,
                                      uint16_t *from, uint16_t *to,
                                      uint8_t *forward_flag) {
    uint16_t right, mid = 0, left, max;
    uint16_t que_id;
    uint8_t  *ptr, *p;
    NJ_CHAR  *str;
    int16_t ret = 0;
    int32_t found = 0;
    uint8_t slen;
    int32_t cmp;


    
    ptr = get_search_index_address(handle, op);
    
    max = GET_LEARN_WORD_COUNT(handle);

    right = sto;
    left = sfrom;

#ifdef LEARN_DEBUG
    printf("src:[%s]\n", yomi);
#endif 

    *forward_flag = 0;

    while (left <= right) {
        mid = left + ((right - left) / 2);
        p = ptr + (mid * NJ_INDEX_SIZE);
        que_id = GET_UINT16(p);
        str = get_string(iwnn, handle, que_id, &slen);
        if (str == NULL) {
            return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_DIC_BROKEN); 
        }

        
        ret = nj_strncmp(yomi, str, len);

#ifdef LEARN_DEBUG
        printf("   [%d][%d][%d]COMPARE:[%s] = %d\n", left, mid, right, str, ret);
#endif 
        if (ret == 0) {
            
            found = 1;
            break;
        } else if (ret < 0) {
            
            right = mid - 1;
            if (mid == 0) {
                break;
            }
        } else {
            
            left = mid + 1;
        }
    }

    if (!found) {
        return 0;
    }

    if (mid == 0) {
        *from = mid;
    } else {
        
        p = ((mid - 1) * NJ_INDEX_SIZE) + ptr;
        
        for (cmp = mid - 1; cmp >= 0; cmp--) {
            que_id = GET_UINT16(p);
            str = get_string(iwnn, handle, que_id, &slen);
            if (str == NULL) {
                return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_DIC_BROKEN); 
            }

            
            if (nj_strncmp(yomi, str, len) != 0) {
                break;      
            }
            p -= NJ_INDEX_SIZE;
        }
        if (cmp < 0) {
            *from = 0;
        } else {
            *from = (uint16_t)cmp + 1;
        }
    }

#ifdef LEARN_DEBUG
    printf("  >> from:(%d)\n", *from);
#endif 

    if ((mid + 1) >= max) {
        *to = mid;
    } else {
        
        p = ((mid + 1) * NJ_INDEX_SIZE) + ptr;
        
        for (right = mid + 1; right < max; right++) {
            que_id = GET_UINT16(p);
            str = get_string(iwnn, handle, que_id, &slen);
            if (str == NULL) {
                return NJ_SET_ERR_VAL(NJ_FUNC_SEARCH_RANGE_BY_YOMI, NJ_ERR_DIC_BROKEN); 
            }

            
            if (nj_strncmp(yomi, str, len) != 0) {
                break;      
            }
            p += NJ_INDEX_SIZE;
        }
        *to = right - 1;
    }

#ifdef LEARN_DEBUG
    printf("  >> to:(%d)\n", *to);
#endif 
    return 1;
}

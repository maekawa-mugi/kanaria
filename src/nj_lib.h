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

#define NJ_VERSION "iWnn Version 1.1.2"

#ifndef _NJ_LIB_H_
#define _NJ_LIB_H_

#include <stdint.h>

typedef uint32_t NJ_CHAR;

#define NJ_CHAR_NUL  0x0000

#define NJ_TERM_LEN  1
#define NJ_TERM_SIZE (NJ_TERM_LEN)

#ifndef NULL
#define NULL 0
#endif

#ifdef NJ_STACK_CHECK_FILE
typedef NJ_VOID VOID;
#endif 

#ifndef NJ_CHAR_WAVE_DASH_BIG
#define NJ_CHAR_WAVE_DASH_BIG   0xFF5E 
#endif 
#ifndef NJ_CHAR_WAVE_DASH_SMALL
#define NJ_CHAR_WAVE_DASH_SMALL 0x007E 
#endif 

typedef int16_t NJ_HINDO;

#define NJ_INDEX_SIZE      2

#define NJ_LEARN_DIC_HEADER_SIZE   72

#ifndef NJ_MAX_DIC
#define NJ_MAX_DIC 20
#endif 

#ifndef NJ_MAX_CHARSET
#define NJ_MAX_CHARSET 200
#endif 

#ifndef NJ_SEARCH_CACHE_SIZE
#define NJ_SEARCH_CACHE_SIZE   200
#endif 
 
#ifndef NJ_CACHE_VIEW_CNT
#define NJ_CACHE_VIEW_CNT       2
#endif 


#ifndef NJ_MAX_RESULT_LEN
#define NJ_MAX_RESULT_LEN  50
#endif 

#ifndef NJ_MAX_LEN
#define NJ_MAX_LEN          50
#endif 

#ifndef NJ_MAX_KEYWORD
#define NJ_MAX_KEYWORD (NJ_MAX_RESULT_LEN + NJ_TERM_LEN)
#endif 

#ifndef NJ_MAX_PHRASE
#define NJ_MAX_PHRASE       NJ_MAX_LEN
#endif 

#ifndef NJ_MAX_PHR_CONNECT
#define NJ_MAX_PHR_CONNECT      5
#endif 

#ifndef NJ_MAX_USER_LEN
#define NJ_MAX_USER_LEN         50
#endif 

#ifndef NJ_MAX_USER_KOUHO_LEN
#define NJ_MAX_USER_KOUHO_LEN   50
#endif 

#ifndef NJ_MAX_USER_COUNT
#define NJ_MAX_USER_COUNT       100
#endif 

#define NJ_USER_QUE_SIZE        (((NJ_MAX_USER_LEN + NJ_MAX_USER_KOUHO_LEN) * sizeof(NJ_CHAR)) + 5)
#define NJ_USER_DIC_SIZE        ((NJ_USER_QUE_SIZE + NJ_INDEX_SIZE + NJ_INDEX_SIZE) * NJ_MAX_USER_COUNT + NJ_INDEX_SIZE  + NJ_INDEX_SIZE + NJ_LEARN_DIC_HEADER_SIZE + 4)

typedef const uint8_t *NJ_DIC_HANDLE;

typedef struct {
    uint16_t base;
    uint16_t high;
} NJ_DIC_FREQ;

typedef struct {
    uint32_t  current;
    uint32_t  top;
    uint32_t  bottom;
    uint8_t  *node;
    uint8_t  *now;
    uint16_t  idx_no;
} NJ_CACHE_INFO;

typedef struct {
    uint8_t      statusFlg;
#define NJ_STATUSFLG_CACHEOVER ((uint8_t)0x01)
#define NJ_STATUSFLG_AIMAI     ((uint8_t)0x02)
#define NJ_STATUSFLG_HINDO     ((uint8_t)0x04)
    uint8_t      viewCnt;
    uint16_t     keyPtr[NJ_MAX_KEYWORD];
    NJ_CACHE_INFO storebuff[NJ_SEARCH_CACHE_SIZE];  
} NJ_SEARCH_CACHE;

#define NJ_GET_CACHEOVER_FROM_SCACHE(s) ((s)->statusFlg & NJ_STATUSFLG_CACHEOVER)
#define NJ_GET_AIMAI_FROM_SCACHE(s)     ((s)->statusFlg & NJ_STATUSFLG_AIMAI)
#define NJ_SET_CACHEOVER_TO_SCACHE(s)   ((s)->statusFlg |= NJ_STATUSFLG_CACHEOVER)
#define NJ_SET_AIMAI_TO_SCACHE(s)       ((s)->statusFlg |= NJ_STATUSFLG_AIMAI)
#define NJ_UNSET_CACHEOVER_TO_SCACHE(s) ((s)->statusFlg &= ~NJ_STATUSFLG_CACHEOVER) 
#define NJ_UNSET_AIMAI_TO_SCACHE(s)     ((s)->statusFlg &= ~NJ_STATUSFLG_AIMAI)


typedef struct {
    uint8_t           type;
#define NJ_DIC_H_TYPE_NORMAL   0x00     
    uint8_t           limit;

    NJ_DIC_HANDLE       handle;         

#define NJ_MODE_TYPE_MAX  1   
    
    NJ_DIC_FREQ         dic_freq[NJ_MODE_TYPE_MAX];
#define NJ_MODE_TYPE_HENKAN  0   

    NJ_SEARCH_CACHE *   srhCache;       
} NJ_DIC_INFO;


typedef struct {
    NJ_DIC_INFO   dic[NJ_MAX_DIC];           
    NJ_DIC_HANDLE  rHandle[NJ_MODE_TYPE_MAX]; 

    
    uint16_t           mode;
#define NJ_CACHE_MODE_NONE          0x0000    
#define NJ_CACHE_MODE_VALID         0x0001    

    
    NJ_CHAR             keyword[NJ_MAX_KEYWORD];
} NJ_DIC_SET;

typedef struct {
    uint16_t  charset_count;
    NJ_CHAR    *from[NJ_MAX_CHARSET];       
    NJ_CHAR    *to[NJ_MAX_CHARSET];         
} NJ_CHARSET;


typedef struct {

    uint8_t operation;
#define NJ_CUR_OP_COMP      0    
#define NJ_CUR_OP_FORE      1    
#define NJ_CUR_OP_LINK      2    

    uint8_t mode;
#define NJ_CUR_MODE_FREQ    0    
#define NJ_CUR_MODE_YOMI    1    

    NJ_DIC_SET *ds;              

    struct {
        uint8_t *fore;
        uint16_t foreSize;
        uint16_t foreFlag;
        uint8_t *rear;
        uint16_t rearSize;
        uint16_t rearFlag;
        const uint8_t *yominasi_fore;
    } hinsi;

    NJ_CHAR  *yomi;
    uint16_t ylen;
    uint16_t yclen;
    NJ_CHAR  *kanji;      

    NJ_CHARSET *charset;  

} NJ_SEARCH_CONDITION;

typedef struct {
    NJ_DIC_HANDLE  handle;        
    uint32_t      current;
    uint32_t      top;
    uint32_t      bottom;
    uint32_t      relation[NJ_MAX_PHR_CONNECT];
    uint8_t       current_cache;
    uint8_t       current_info;
    uint8_t       status;
    uint8_t       type;
} NJ_SEARCH_LOCATION;

typedef struct {
    NJ_HINDO           cache_freq;   
    NJ_DIC_FREQ        dic_freq;     
    NJ_SEARCH_LOCATION loct;         
} NJ_SEARCH_LOCATION_SET;

typedef struct {
    NJ_SEARCH_CONDITION cond;                   
    NJ_SEARCH_LOCATION_SET loctset[NJ_MAX_DIC]; 
} NJ_CURSOR;


typedef struct {
    uint8_t hinsi_group;
#define NJ_HINSI_MEISI          0    
#define NJ_HINSI_JINMEI         1    
#define NJ_HINSI_MEISI_NO_CONJ  2    
#define NJ_HINSI_CHIMEI         2    
#define NJ_HINSI_KIGOU          3    

    NJ_CHAR  yomi[NJ_MAX_LEN + NJ_TERM_LEN];         
    NJ_CHAR  kouho[NJ_MAX_RESULT_LEN + NJ_TERM_LEN]; 

    
    struct {
        uint16_t  yomi_len;
        uint16_t  kouho_len;
        uint32_t  hinsi;
        uint32_t  attr;
        int16_t   freq;
    } stem;

    
    struct {
        uint16_t  yomi_len;
        uint16_t  kouho_len;
        uint32_t  hinsi;
        int16_t   freq;
    } fzk;

    int16_t   connect;

} NJ_WORD_INFO;

typedef struct {
    NJ_CHAR  *yomi;   

    
    struct NJ_STEM {
        uint16_t  info1;
        uint16_t  info2;
        NJ_HINDO   hindo;       
        NJ_SEARCH_LOCATION loc; 
        uint8_t   type;
    } stem;

    
    struct NJ_FZK {
        uint16_t  info1;
        uint16_t  info2;
        NJ_HINDO   hindo;       
    } fzk;
} NJ_WORD;

#define NJ_GET_FPOS_FROM_STEM(s) ((uint16_t)((s)->stem.info1 >> 7))
#define NJ_GET_BPOS_FROM_STEM(s) ((uint16_t)((s)->stem.info2 >> 7))


#define NJ_SET_FPOS_TO_STEM(s,v) ((s)->stem.info1 = ((s)->stem.info1 & 0x007F) | (uint16_t)((v) << 7))
#define NJ_GET_YLEN_FROM_STEM(s) ((uint8_t)((s)->stem.info1 & 0x7F))
#define NJ_GET_KLEN_FROM_STEM(s) ((uint8_t)((s)->stem.info2 & 0x7F))
#define NJ_SET_YLEN_TO_STEM(s,v) ((s)->stem.info1 = ((s)->stem.info1 & 0xFF80) | (uint16_t)((v) & 0x7F))
#define NJ_SET_BPOS_TO_STEM(s,v) ((s)->stem.info2 = ((s)->stem.info2 & 0x007F) | (uint16_t)((v) << 7))
#define NJ_SET_KLEN_TO_STEM(s,v) ((s)->stem.info2 = ((s)->stem.info2 & 0xFF80) | (uint16_t)((v) & 0x7F))

#define NJ_GET_YLEN_FROM_FZK(f) ((uint8_t)((f)->fzk.info1 & 0x7F))
#define NJ_GET_BPOS_FROM_FZK(f) ((uint16_t)((f)->fzk.info2 >> 7))

typedef struct {
    
    uint16_t operation_id;
#define NJ_OP_MASK          0x000f  
#define NJ_GET_RESULT_OP(id) ((id) & NJ_OP_MASK)
#define NJ_OP_SEARCH        0x0000  

#define NJ_FUNC_MASK        0x00f0  
#define NJ_GET_RESULT_FUNC(id) ((id) & NJ_FUNC_MASK)
#define NJ_FUNC_SEARCH              0x0000  

#define NJ_DIC_MASK                 0xf000  
#define NJ_GET_RESULT_DIC(id) ((id) & 0xF000)
#define NJ_DIC_STATIC               0x1000  
#define NJ_DIC_CUSTOMIZE            0x2000  
#define NJ_DIC_LEARN                0x3000  
#define NJ_DIC_USER                 0x4000  

    
    NJ_WORD word;
} NJ_RESULT;

typedef struct {
    uint16_t  mode;
#define NJ_DEFAULT_MODE (NJ_NO_RENBUN|NJ_NO_TANBUN|NJ_RELATION_ON|NJ_YOMINASI_ON)
    uint16_t  forecast_learn_limit;
#define NJ_DEFAULT_FORECAST_LEARN_LIMIT 30      
    uint16_t  forecast_limit;
#define NJ_DEFAULT_FORECAST_LIMIT 100           
    uint8_t   char_min;
#define NJ_DEFAULT_CHAR_MIN 0                   
    uint8_t   char_max;
#define NJ_DEFAULT_CHAR_MAX NJ_MAX_LEN          
} NJ_ANALYZE_OPTION;

#define NJ_STATE_MAX_FREQ  1000         
#define NJ_STATE_MIN_FREQ     0         

#include "njx_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NJ_EXTERN extern

NJ_EXTERN int16_t njx_get_stroke(NJ_CLASS *iwnn, NJ_RESULT *result, NJ_CHAR  *buf, uint16_t buf_size);
NJ_EXTERN int16_t njx_get_candidate(NJ_CLASS *iwnn, NJ_RESULT *result, NJ_CHAR  *buf, uint16_t buf_size);
NJ_EXTERN int16_t njx_search_word(NJ_CLASS *iwnn, NJ_CURSOR *cursor);
NJ_EXTERN int16_t njx_get_word(NJ_CLASS *iwnn, NJ_CURSOR *cursor, NJ_RESULT *result);
NJ_EXTERN int16_t njx_check_dic(NJ_CLASS *iwnn, NJ_DIC_HANDLE handle, uint8_t restore, uint32_t size);

NJ_EXTERN int16_t njx_init(NJ_CLASS *iwnn);
NJ_EXTERN int16_t njx_select(NJ_CLASS *iwnn, NJ_RESULT *r_result);

#ifdef __cplusplus
} // extern "C"
#endif

#endif 

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



static int16_t set_previous_selection(NJ_CLASS *iwnn, NJ_RESULT *result);
static int16_t set_learn_word_info(NJ_CLASS *iwnn, NJ_LEARN_WORD_INFO *lword, NJ_RESULT *result);



NJ_EXTERN int16_t njx_select(NJ_CLASS *iwnn, NJ_RESULT *r_result) {
    int16_t ret;
    NJ_DIC_SET *dics;


    if (iwnn == NULL) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_NJ_SELECT, NJ_ERR_PARAM_ENV_NULL);
    }
    dics = &(iwnn->dic_set);

    if (dics->rHandle[NJ_MODE_TYPE_HENKAN] == NULL) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJ_SELECT, NJ_ERR_NO_RULEDIC);
    }

    
    if ( r_result != NULL ) {
        
        ret = set_previous_selection(iwnn, r_result);
        if (ret < 0) {
            return ret; 
        }
    } else {
        
        set_previous_selection(iwnn, NULL);
    }
    return 0;   
}

NJ_EXTERN int16_t njx_init(NJ_CLASS *iwnn) {

    if (iwnn == NULL) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_NJ_INIT, NJ_ERR_PARAM_ENV_NULL);
    }

    
    set_previous_selection(iwnn, NULL);
    return 0;
}

NJ_EXTERN int16_t njx_get_candidate(NJ_CLASS *iwnn, NJ_RESULT *result, NJ_CHAR *buf, uint16_t buf_size) {
    int16_t ret;


    if (iwnn == NULL) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_NJ_GET_CANDIDATE, NJ_ERR_PARAM_ENV_NULL);
    }
    if (result == NULL) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJ_GET_CANDIDATE, NJ_ERR_PARAM_RESULT_NULL);
    }

    if ((buf == NULL) || (buf_size == 0)) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJ_GET_CANDIDATE, NJ_ERR_BUFFER_NOT_ENOUGH);
    }

    switch (NJ_GET_RESULT_OP(result->operation_id)) {
    case NJ_OP_SEARCH:
        ret = njd_get_candidate(iwnn, result, buf, buf_size);
        break;

    default:
        
        ret = NJ_SET_ERR_VAL(NJ_FUNC_NJ_GET_CANDIDATE, NJ_ERR_INVALID_RESULT); 
        break;
    }
    
    return ret;
}

NJ_EXTERN int16_t njx_get_stroke(NJ_CLASS *iwnn, NJ_RESULT *result, NJ_CHAR *buf, uint16_t buf_size) {
    int16_t ret;


    if (iwnn == NULL) {
        
        return NJ_SET_ERR_VAL(NJ_FUNC_NJ_GET_STROKE, NJ_ERR_PARAM_ENV_NULL);
    }
    if (result == NULL) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJ_GET_STROKE, NJ_ERR_PARAM_RESULT_NULL);
    }

    if ((buf == NULL) || (buf_size == 0)) {
        return NJ_SET_ERR_VAL(NJ_FUNC_NJ_GET_STROKE, NJ_ERR_BUFFER_NOT_ENOUGH);
    }

    switch (NJ_GET_RESULT_OP(result->operation_id)) {
    case NJ_OP_SEARCH:
        ret = njd_get_stroke(iwnn, result, buf, buf_size);
        break;

    default:
        
        ret = NJ_SET_ERR_VAL(NJ_FUNC_NJ_GET_STROKE, NJ_ERR_INVALID_RESULT); 
        break;
    }
    return ret;
}


static int16_t set_previous_selection(NJ_CLASS *iwnn, NJ_RESULT *result) {
    int16_t   ret;
    NJ_PREVIOUS_SELECTION_INFO *prev_info = &(iwnn->previous_selection);


    if (result == NULL) {
        prev_info->count = 0;
   } else {
        ret = set_learn_word_info(iwnn, &(prev_info->selection_data), result);
        if (ret < 0) {
            
            return ret; 
        }

        prev_info->count = 1;
    }
    
    return 0;
}

static int16_t set_learn_word_info(NJ_CLASS *iwnn, NJ_LEARN_WORD_INFO *lword, NJ_RESULT *result)
{
    int16_t ret;
    NJ_DIC_SET *dics = &(iwnn->dic_set);



#if 0
    
    ret = njx_get_stroke(iwnn, result, lword->yomi, sizeof(lword->yomi));
    if (ret < 0) {
        return ret; 
    }
    lword->yomi_len = (uint8_t)ret;
    ret = njx_get_candidate(iwnn, result, lword->hyouki, sizeof(lword->hyouki));
    if (ret < 0) {
        return ret; 
    }
    lword->hyouki_len = (uint8_t)ret;
#else
    lword->yomi[0] = 0x0000;
    lword->yomi_len = 0;
    lword->hyouki[0] = 0x0000;
    lword->hyouki_len = 0;
#endif

    
    lword->left_connection_id = NJ_GET_LEFT_CONNECTION_ID_FROM_STEM(&(result->word));
    lword->stem_right_connection_id = NJ_GET_RIGHT_CONNECTION_ID_FROM_STEM(&(result->word));
    lword->right_connection_id = NJ_GET_RIGHT_CONNECTION_ID_FROM_STEM(&(result->word));

    
    ret = njd_r_get_connection_id(dics->rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_SINGLE_KANJI_LEFT);
    if ((ret != 0) && (lword->left_connection_id == (uint16_t)ret)) {
        ret = njd_r_get_connection_id(dics->rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_PLACE_NAME_LEFT);
        if (ret != 0) {
            lword->left_connection_id = (uint16_t)ret;
        }
    }

    
    ret = njd_r_get_connection_id(dics->rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_SINGLE_KANJI_RIGHT);
    if ((ret != 0) && (lword->right_connection_id == (uint16_t)ret)) {
        ret = njd_r_get_connection_id(dics->rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_PLACE_NAME_RIGHT);
        if (ret != 0) {
            lword->right_connection_id = (uint16_t)ret;
        }
    }

    
    ret = njd_r_get_connection_id(dics->rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_SINGLE_KANJI_RIGHT);
    if ((ret != 0) && (lword->stem_right_connection_id == (uint16_t)ret)) {
        ret = njd_r_get_connection_id(dics->rHandle[NJ_MODE_TYPE_HENKAN], NJ_CONNECTION_PLACE_NAME_RIGHT);
        if (ret != 0) {
            lword->stem_right_connection_id = (uint16_t)ret;
        }
    }

    return 0;

}

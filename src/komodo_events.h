/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/
#pragma once
#include "squishy_defs.h"
#include "squishy_structs.h"

void squishy_eventadd_notarized(squishy_state *sp, const char *symbol,int32_t height, squishy::event_notarized& ntz);

void squishy_eventadd_pubkeys(squishy_state *sp, const char *symbol,int32_t height, squishy::event_pubkeys& pk);

void squishy_eventadd_pricefeed(squishy_state *sp, const char *symbol,int32_t height, squishy::event_pricefeed& pf);

void squishy_eventadd_opreturn(squishy_state *sp, const char *symbol,int32_t height, squishy::event_opreturn& opret);

void squishy_eventadd_kmdheight(squishy_state *sp, const char *symbol,int32_t height, squishy::event_kmdheight& kmd_ht);

void squishy_event_rewind(squishy_state *sp, const char *symbol,int32_t height);

void squishy_setkmdheight(squishy_state *sp,int32_t kmdheight,uint32_t timestamp);

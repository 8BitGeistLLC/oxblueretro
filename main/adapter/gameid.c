/*
 * Copyright (c) 2019-2022, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "gameid.h"

#define GAME_ID_BUF_LEN 23

static char gameid[GAME_ID_BUF_LEN] = {0};
static char tmp_gameid[GAME_ID_BUF_LEN] = {0};

int32_t gid_update_sys(struct raw_fb *fb_data) {
    memset(tmp_gameid, 0, sizeof(gameid));
    memcpy(tmp_gameid, fb_data->data, fb_data->header.data_len);

    if (memcmp(gameid, tmp_gameid, sizeof(gameid)) != 0) {
        memcpy(gameid, tmp_gameid, sizeof(gameid));
        printf("# %s: %s\n", __FUNCTION__, gameid);
        return 1;
    }
    return 0;
}

char *gid_get(void) {
    return gameid;
}

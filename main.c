#pragma codeseg _CODE

__sfr __at 0x4 io4;
__sfr __at 0x5 io5;
__sfr __at 0x6 io6;
__sfr __at 0x7 io7;
__sfr __at 0xC ioC;

void write45(unsigned short data) {
    io4 = data >> 8;
    while (io4 & 0x80) {}
    io5 = data & 0xff;
    while (io4 & 0x80) {}
}

void write67(unsigned short data) {
    io6 = data >> 8;
    while (io4 & 0x80) {}
    io7 = data & 0xff;
    while (io4 & 0x80) {}
}

typedef struct {
    unsigned short begin;
    unsigned short end;
} fxSample_t;

typedef struct {
    unsigned char prioAndBehaviour;
    unsigned char sampleIdx;
    unsigned char panAndVol;
} effectsTableEntry_t;

typedef struct {
    char **patterns;
} bgmChannelMatrix_t;

typedef struct {
    char **channels[12];
} bgmMainMatrix_t;

typedef struct {
    unsigned char ext2mode;
    unsigned char fmUsage;
    unsigned char pcmUsage;
    unsigned char unknown0;
    unsigned char evenTicks;
    unsigned char oddTicks;
    unsigned char rowsPerPattern; // number of ticks to spend per pattern
    unsigned char matrixSize; // number of patterns in channel matrices
    void *fmInstruments;
    void *ssgInstruments;
    bgmMainMatrix_t *mainMatrix;
} bgmTrack_t;

typedef struct {
    unsigned short begin;
    unsigned short end;
} pcmSample_t;

typedef struct {
    fxSample_t *fxSamples;
    effectsTableEntry_t *effectsTable0;
    effectsTableEntry_t *effectsTable1;
    effectsTableEntry_t *effectsTable2;
    pcmSample_t *pcmSamples;
    unsigned short padding[3];
    unsigned char ssgVolumeTranslate[16];
    bgmTrack_t *bgmTracks[64];
} dataSection_t;

#define DATA ((dataSection_t *)0x4000)

typedef struct {
    unsigned short note_length; // number of ticks remaining for current note
    unsigned char *next_note; 
} driver_bgm_channel_state_t;

typedef struct {
    unsigned short beat_countdown; // number of ticks to next beat
    bgmTrack_t *current_track;
    unsigned short current_pattern_index;
    unsigned short current_pattern_length; // number of ticks remaining for current pattern
    driver_bgm_channel_state_t channels[12];
} driver_bgm_state_t;

#define CMD_BUFFER_CAP 16

typedef struct {
    volatile unsigned char cmd_buffer_read_select;
    volatile unsigned char cmd_buffer_lengths[2];
    volatile unsigned char cmd_buffers[2][CMD_BUFFER_CAP];
    
    unsigned short tick_counter;

    unsigned char adpcma_channel_status;
    unsigned char adpcma_channel_bgm_reserved;
    unsigned char adpcma_channel_commands[6];
    unsigned char adpcma_channel_priorities[6];

    driver_bgm_state_t bgm_state;
} driver_state_t;

#define STATE ((driver_state_t *)0xf800)

static void updateChannelStatus(void) {
    unsigned char flags = io6 & 0x3f; // read raised flags

    write45(0x1C00 | flags); // reset and mask raised flags
    write45(0x1C00); // unmask all flags

    STATE->adpcma_channel_status &= ~flags; // set raised channels to false
}

#define BEHAVIOUR_DUPLICATE 0x4
#define BEHAVIOUR_RETRIGGER 0x6

static void processFXCommand(unsigned short cmd) {
    effectsTableEntry_t *entry = &(DATA->effectsTable0[cmd - 0x60]);
    fxSample_t *sample = &(DATA->fxSamples[entry->sampleIdx]);

    unsigned char priority = entry->prioAndBehaviour & 0xf0;
    unsigned char behaviour = entry->prioAndBehaviour & 0xf;

    unsigned char channelIdx = 6; // we're using 6 here to mean "unassigned"

    unsigned char lowestPriorityChannelIdx = 0;
    unsigned char lowestPriorityChannelPriority = 0xFF;

    if (behaviour == BEHAVIOUR_RETRIGGER) {
        // if the behaviour is to retrigger, see if we're already playing the effect on any channel
        for (channelIdx = 0; channelIdx < 6; channelIdx++) {
            unsigned char isPlaying = (STATE->adpcma_channel_status >> channelIdx) & 1;
            unsigned char isReserved = (STATE->adpcma_channel_bgm_reserved >> channelIdx) & 1;
            if (isPlaying && !isReserved) {
                if (STATE->adpcma_channel_commands[channelIdx] == cmd) {
                    // stop channel playback
                    write67(0x0080 | (1 << channelIdx));
                    // break to schedule playback on this channel
                    break;
                }
            }
        }
    }

    if (channelIdx == 6) {
        for (channelIdx = 0; channelIdx < 6; channelIdx++) {
            unsigned char isPlaying = (STATE->adpcma_channel_status >> channelIdx) & 1;
            unsigned char isReserved = (STATE->adpcma_channel_bgm_reserved >> channelIdx) & 1;
            if (isPlaying && !isReserved) {
                // keep track of lowest priority channel encountered
                if (STATE->adpcma_channel_priorities[channelIdx] < lowestPriorityChannelPriority) {
                    lowestPriorityChannelIdx = channelIdx;
                    lowestPriorityChannelPriority = STATE->adpcma_channel_priorities[channelIdx];
                }
            } else {
                // we found an idle channel, use it
                break;
            }
        }
    }

    if (channelIdx == 6) {
        // all channels busy, but see if there is a channel with a lower priority we can overwrite
        if (lowestPriorityChannelPriority < priority) {
            // stop channel playback
            write67(0x0080 | (1 << lowestPriorityChannelIdx));
            // select channel id to be overwritten
            channelIdx = lowestPriorityChannelIdx;
        } else {
            return; // no suitable channel found, skip playback
        }
    }

    write67(((0x08 + channelIdx) << 8) | entry->panAndVol);
    write67(((0x10 + channelIdx) << 8) | (sample->begin & 0xff));
    write67(((0x18 + channelIdx) << 8) | (sample->begin >> 8));
    write67(((0x20 + channelIdx) << 8) | (sample->end & 0xff));
    write67(((0x28 + channelIdx) << 8) | (sample->end >> 8));

    STATE->adpcma_channel_status |= (1 << channelIdx);
    STATE->adpcma_channel_commands[channelIdx] = cmd;
    STATE->adpcma_channel_priorities[channelIdx] = priority;

    // start channel playback by selecting the channel (setting the nth bit)
    // and setting the dump flag (7th bit) to 0
    write67(0x0000 | (1 << channelIdx));
}

static void processADPCMSystemCommand(unsigned short cmd) {
    switch (cmd) {
        case 0x15: // ADPCM_SET_MAX_VOL
            write67(0x013F);
            break;
    }
}

static void processBGMCommand(unsigned short cmd) {
    bgmTrack_t *track = DATA->bgmTracks[cmd - 0x20];

    STATE->bgm_state.current_track = track;
    STATE->bgm_state.beat_countdown = 0;
    STATE->bgm_state.current_pattern_index = 0;
    STATE->bgm_state.current_pattern_length = track->rowsPerPattern;

    // reserve PCM channels needed for track playback
    STATE->adpcma_channel_bgm_reserved = track->pcmUsage;

    for (unsigned char i = 0; i < 12; i++) {
        if (track->mainMatrix->channels[i] != 0) {
            STATE->bgm_state.channels[i].note_length = 0;
            STATE->bgm_state.channels[i].next_note = track->mainMatrix->channels[i][0];
        }
    }
}

static void processTick(void) {
    bgmTrack_t *track = STATE->bgm_state.current_track;

    if (track != 0) {
        if (STATE->bgm_state.beat_countdown == 0) {
            // we're on a beat, so reset counter and run the rest of the routine
            STATE->bgm_state.beat_countdown = track->evenTicks + track->oddTicks;
        } else {
            // we're between beats, so bump counter and exit
            STATE->bgm_state.beat_countdown--;
            return;
        }

        // if pattern length is zero, advance to next pattern
        if (STATE->bgm_state.current_pattern_length == 0) {
            unsigned short current_pattern_index = ++STATE->bgm_state.current_pattern_index;

            if (track->matrixSize == current_pattern_index) {
                // end of track
                STATE->bgm_state.current_track = 0;
                STATE->adpcma_channel_bgm_reserved = 0;
                return;
            }

            STATE->bgm_state.current_pattern_length = track->rowsPerPattern - 1;

            for (unsigned char i = 0; i < 12; i++) {
                STATE->bgm_state.channels[i].note_length = 0;
                STATE->bgm_state.channels[i].next_note = track->mainMatrix->channels[i][current_pattern_index];
            }
        } else {
            STATE->bgm_state.current_pattern_length--;
        }

        for (unsigned char i = 7; i < 12; i++) {
            if (track->mainMatrix->channels[i] != 0) {
                driver_bgm_channel_state_t *channel_state = &STATE->bgm_state.channels[i];

                // if note length is zero, advance to next note
                if (channel_state->note_length == 0) {
                    unsigned char note = channel_state->next_note[0];
                    unsigned char pcmChannelIdx = i - 7;

                    if (note < 42) {
                        write67(0x0080 | (1 << pcmChannelIdx)); // stop sample playback
                        write67(((0x08 + pcmChannelIdx) << 8) | 0xfd);
                        write67(((0x10 + pcmChannelIdx) << 8) | (DATA->pcmSamples[note-1].begin & 0xff));
                        write67(((0x18 + pcmChannelIdx) << 8) | (DATA->pcmSamples[note-1].begin >> 8));
                        write67(((0x20 + pcmChannelIdx) << 8) | (DATA->pcmSamples[note-1].end & 0xff));
                        write67(((0x28 + pcmChannelIdx) << 8) | (DATA->pcmSamples[note-1].end >> 8));
                        write67(0x0000 | (1 << pcmChannelIdx)); // start sample playback
                    } else if (note == 0x7f) {
                        write67(0x0080 | (1 << pcmChannelIdx)); // stop sample playback
                    }

                    if (note == 0xfe) {
                        channel_state->note_length = channel_state->next_note[2];
                        channel_state->next_note = &channel_state->next_note[4];
                    } else {
                        if (channel_state->next_note[3] == 0x0e) {
                            channel_state->note_length = channel_state->next_note[4];
                            channel_state->next_note = &channel_state->next_note[6];
                        } else {
                            channel_state->note_length = channel_state->next_note[6];
                            channel_state->next_note = &channel_state->next_note[8];
                        }
                    }
                } else {
                    channel_state->note_length--;
                }
            }
        }
    }
}

void mainLoop(void) {
    for (;;) {
        if (STATE->tick_counter == 0) {
            processTick();
        }

        updateChannelStatus();

        unsigned short cmd = 0;
        unsigned short sel = STATE->cmd_buffer_read_select;

        if (STATE->cmd_buffer_lengths[sel] > 0) {
            cmd = STATE->cmd_buffers[sel][STATE->cmd_buffer_lengths[sel] - 1];
            STATE->cmd_buffer_lengths[sel]--;
        } else {
            STATE->cmd_buffer_read_select ^= 1;
        }

        if (cmd >= 0x11 && cmd <= 0x1f) {
            processADPCMSystemCommand(cmd);
        }

        if (cmd >= 0x20 && cmd < 0x60) {
            processBGMCommand(cmd);
        }

        if (cmd >= 0x60) {
            processFXCommand(cmd);
        }

        if (STATE->tick_counter == 0) {
            STATE->tick_counter = 32;
        } else {
            STATE->tick_counter = (STATE->tick_counter + 1) % 64;
        }
    }
}

void queueCommand(unsigned short cmd) {
    // To provide interrupt safety to the main loop, we use a double-buffering
    // strategy here:
    // 
    // This function, which is called as part of the NMI routine, only modifies
    // the buffer (and associated length) that was assigned to it by the main loop.
    //
    // The main loop will read the "foreground" buffer until it is empty, and then
    // switch buffers by toggling the buffer select bit, while this function will
    // only ever modify the "background" buffer.
    //
    // This exclusivity prevents any data races between interrupt routine and main
    // loop.
    unsigned short sel = STATE->cmd_buffer_read_select ^ 1;
    if (STATE->cmd_buffer_lengths[sel] < CMD_BUFFER_CAP) {
        STATE->cmd_buffers[sel][STATE->cmd_buffer_lengths[sel]] = cmd;
        STATE->cmd_buffer_lengths[sel]++;
    }
}

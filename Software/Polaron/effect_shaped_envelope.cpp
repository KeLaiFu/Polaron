// Copyright (c) 2018 Thomas Zueblin
//
// Author: Thomas Zueblin (thomas.zueblin@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#include "effect_shaped_envelope.h"
#include <Arduino.h>
#include "utility/dspinst.h"

#define STATE_IDLE 0
#define STATE_ATTACK 1
#define STATE_HOLD 2
#define STATE_DECAY 3
#define STATE_FORCED 4

void AudioEffectShapedEnvelope::noteOn(void) {
    __disable_irq();
    if (state == STATE_IDLE || decay_forced_count == 0) {
        state = STATE_ATTACK;
        count = attack_count;
        phase_increment = (4294967296 / count);
        phase_accumulator = 0;
        triggerCount = maxRetriggers;
        calculateLinearTransformFactors(0, 32767);
        // Serial.print("NOTE-ON-ATTACK");
    } else if (state != STATE_FORCED) {
        state = STATE_FORCED;
        count = decay_forced_count;
        phase_increment = (4294967296 / count);
        phase_accumulator = 0;
        calculateLinearTransformFactors(32767, 0);
        // Serial.print("NOTE-ON-FORCED");
    }
    __enable_irq();
}

void AudioEffectShapedEnvelope::update(void) {
    audio_block_t *block;
    int16_t *p, *end;
    uint32_t index, scale;
    int32_t val1, val2, interpolatedVal, transformedVal;

    int16_t sample;

    block = receiveWritable();
    if (!block) return;
    if (state == STATE_IDLE) {
        release(block);
        return;
    }
    p = (int16_t *)(block->data);
    end = p + AUDIO_BLOCK_SAMPLES;

    // Serial.print("--UPDATE--");
    // Serial.println(count);

    while (p < end) {
        // we only care about the state when completing a region
        if (count == 0) {
            if (state == STATE_ATTACK) {
                count = hold_count;
                if (count > 0) {
                    state = STATE_HOLD;
                    phase_increment = (4294967296 / count);
                    phase_accumulator = 0;
                    calculateLinearTransformFactors(32767, 32767);
                    // Serial.print("HOLD");
                } else {
                    state = STATE_DECAY;
                    count = decay_count;
                    phase_increment = (4294967296 / count);
                    phase_accumulator = 0;
                    calculateLinearTransformFactors(32767, 0);
                    // Serial.print("DECAY");
                }
                continue;
            } else if (state == STATE_HOLD) {
                state = STATE_DECAY;
                count = decay_count;
                phase_increment = (4294967296 / count);
                phase_accumulator = 0;
                calculateLinearTransformFactors(32767, 0);
                // Serial.print("DECAY");
                continue;
            } else if (state == STATE_DECAY) {
                if (triggerCount-- > 0) {
                    state = STATE_ATTACK;
                    count = attack_count;
                    phase_increment = (4294967296 / count);
                    phase_accumulator = 0;
                    calculateLinearTransformFactors(0, 32767);
                } else {
                    state = STATE_IDLE;
                    // Serial.print("IDLE");
                    while (p < end) {
                        *p++ = 0;
                    }
                    break;
                }
            } else if (state == STATE_FORCED) {
                state = STATE_ATTACK;
                count = attack_count;
                phase_increment = (4294967296 / count);
                phase_accumulator = 0;
                calculateLinearTransformFactors(0, 32767);
                // Serial.print("ATTACK");
                continue;
            }
        }

        index = phase_accumulator >> 24;
        val1 = EnvelopeShapeInvertedExponential[index];
        val2 = EnvelopeShapeInvertedExponential[index + 1];
        scale = (phase_accumulator >> 8) & 0xFFFF;
        val2 *= scale;
        val1 *= 0x10000 - scale;

        interpolatedVal = (val1 + val2) >> 16;
        transformedVal = lt_mult * interpolatedVal + lt_add;

        // Serial.print(index);
        // Serial.print(":");
        // Serial.print(transformedVal);
        // Serial.println();

        sample = *p;
        *p++ = (transformedVal * sample) >> 16;
        phase_accumulator += phase_increment;
        count--;
    }
    transmit(block);
    release(block);
}

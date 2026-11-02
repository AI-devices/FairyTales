#include "story_fairy_tale.h"
#include <stddef.h>

#define A       CASE_TXT_01
#define B1      CASE_TXT_02
#define B2      CASE_TXT_03
#define B1_1    CASE_TXT_04
#define B1_2    CASE_TXT_05
#define B2_1    CASE_TXT_06
#define B2_2    CASE_TXT_07
#define B1_1_1  CASE_TXT_08
#define B1_1_2  CASE_TXT_09
#define B1_2_1  CASE_TXT_10
#define B1_2_2  CASE_TXT_11
#define B2_1_1  CASE_TXT_12
#define B2_1_2  CASE_TXT_13
#define B2_2_1  CASE_TXT_14
#define B2_2_2  CASE_TXT_15

#define D1      CASE_TXT_16
#define D2      CASE_TXT_17
#define D3      CASE_TXT_18
#define D4      CASE_TXT_19
#define D5      CASE_TXT_20
#define D6      CASE_TXT_21
#define D7      CASE_TXT_22
#define D8      CASE_TXT_23
#define D9      CASE_TXT_24
#define D10     CASE_TXT_25
#define D11     CASE_TXT_26
#define D12     CASE_TXT_27
#define D13     CASE_TXT_28
#define D14     CASE_TXT_29
#define D15     CASE_TXT_30
#define D16     CASE_TXT_31

static const story_node_t NODES[CASE_TXT_COUNT] = {
    [A] = {
        .question = "Where should she go next ?",
        .choice1  = "To a cottage with smoke from the chimney",
        .choice2  = "To a shining waterfall where she can drink and rest",
        .is_final = false,
        .next1    = B1,
        .next2    = B2,
    },
    [B1] = {
        .question = "What should she do ?",
        .choice1  = "Go inside and see who lives there",
        .choice2  = "Look around the outside to make sure it's safe",
        .is_final = false,
        .next1    = B1_1,
        .next2    = B1_2,
    },
    [B2] = {
        .question = "What should she do ?",
        .choice1  = "Ask the deer to show the way to people",
        .choice2  = "Stay by the waterfall and rest",
        .is_final = false,
        .next1    = B2_1,
        .next2    = B2_2,
    },

    [B1_1] = {
        .question = "What should she do ?",
        .choice1  = "Cook a dinner",
        .choice2  = "Rest on a little bed",
        .is_final = false,
        .next1    = B1_1_1,
        .next2    = B1_1_2,
    },
    [B1_2] = {
        .question = "What should she do ?",
        .choice1  = "Wait for the owners outside",
        .choice2  = "Go inside after all",
        .is_final = false,
        .next1    = B1_2_1,
        .next2    = B1_2_2,
    },
    [B2_1] = {
        .question = "What should she do ?",
        .choice1  = "Knock on the door",
        .choice2  = "Peek in the window",
        .is_final = false,
        .next1    = B2_1_1,
        .next2    = B2_1_2,
    },
    [B2_2] = {
        .question = "What should she do ?",
        .choice1  = "Greet the woman",
        .choice2  = "Hide behind a rock",
        .is_final = false,
        .next1    = B2_2_1,
        .next2    = B2_2_2,
    },

     [B1_1_1] = {
        .question = "What should she do ?",
        .choice1  = "Greet the guests",
        .choice2  = "Hide behind the curtain",
        .is_final = false,
        .next1    = D1,
        .next2    = D2,
    },

     [B1_1_2] = {
        .question = "What will the dwarves do ?",
        .choice1  = "Wake her up",
        .choice2  = "Decide not to disturb her until morning",
        .is_final = false,
        .next1    = D3,
        .next2    = D4,
    },

      [B1_2_1] = {
        .question = "What should she do ?",
        .choice1  = "Call out and introduce herself",
        .choice2  = "Hide behind a bush",
        .is_final = false,
        .next1    = D5,
        .next2    = D6,
    },

      [B1_2_2] = {
        .question = "What should she do ?",
        .choice1  = "Prepare a surprise: clean and decorate the house",
        .choice2  = "Just sit and wait",
        .is_final = false,
        .next1    = D7,
        .next2    = D8,
    },

      [B2_1_1] = {
        .question = "What should she do ?",
        .choice1  = "Answer and enter",
        .choice2  = "Hide and wait",
        .is_final = false,
        .next1    = D9,
        .next2    = D10,
    },

      [B2_1_2] = {
        .question = "What should she do ?",
        .choice1  = "Knock and introduce herself",
        .choice2  = "Stay outside and observe",
        .is_final = false,
        .next1    = D11,
        .next2    = D12,
    },

      [B2_2_1] = {
        .question = "What should she do ?",
        .choice1  = "Take the apple",
        .choice2  = "Refuse and ask who she is",
        .is_final = false,
        .next1    = D13,
        .next2    = D14,
    },

       [B2_2_2] = {
        .question = "What should she do next ?",
        .choice1  = "Follow the old woman's trail",
        .choice2  = "Stay by the waterfall until morning",
        .is_final = false,
        .next1    = D15,
        .next2    = D16,
    },


    [D1]  = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D2]  = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D3]  = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D4]  = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D5]  = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D6]  = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D7]  = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D8]  = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D9]  = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D10] = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D11] = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D12] = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D13] = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D14] = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D15] = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
    [D16] = { .question = "", .choice1 = "", .choice2 = "", .is_final = true },
};

const story_node_t* story_get_node(builtin_text_case_t c) {
    if (c < 0 || c >= CASE_TXT_COUNT) return NULL;
    return &NODES[c];
}

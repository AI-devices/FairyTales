#include "builtin_reading_texts.h"

static const char *const kReadingTexts[CASE_TXT_COUNT] = {
    [CASE_TXT_01] = "Once upon a time, there lived a young princess named Snow White.\n"
                    "She lived with her stepmother, the evil Queen, who every day asked her mirror:\n"
                    "\"Mirror, mirror on the wall, who is the fairest one of all?\"\n"
                    "When the mirror replied that Snow White was the fairest, the Queen flew into a rage and ordered a servant to take the princess into the forest.\n"
                    "The servant took pity on her and said:\n"
                    "\"Run, princess, hide where no one will find you!\"",
    [CASE_TXT_02] = "Snow White walked along the soft path until she saw a little house among the flowers.\n"
                    "Smoke rose from the chimney, and the door was slightly ajar.\n"
                    "Everything looked cozy and calm, as if the cottage was waiting for her.",
    [CASE_TXT_03] = "Snow White approached the waterfall. The air was fresh, and the sound of the water was soothing. A deer stood nearby - beautiful and kind, it was not afraid of her. Snow White felt that magic lived in this place.",
    [CASE_TXT_04] = "Everything was so tiny and neat!\n"
                    "Snow White whispered:\n"
                    "\"Children or magical creatures must live here.\"\n"
                    "She decided to do a kind deed for the owners.",
    [CASE_TXT_05] = "Snow White looked around the cottage but saw no one. A basket of apples lay on the stump, and a lantern was nearby. She sat down and thought, \"Maybe the owners will be back soon?\"",
    [CASE_TXT_06] = "Snow White followed the deer. It walked calmly, as if it knew the way for sure. Soon they came to a clearing where a cottage with a light in the window was visible.",
    [CASE_TXT_07] = "Snow White lit a campfire and warmed her hands. The stares glowed in the sky, and she felt at peace. But suddenly, an old woman with a basket of apples emerged from the darkness and smiled.",
    [CASE_TXT_08] = "Snow White baked pies and set the table.\n"
                    "Suddenly, the door creaked - someone was coming home.\n"
                    "Snow White smiled, but her heart beat faster.",
    [CASE_TXT_09] = "Snow White slept peacefully. At that time, the dwarves quietly entered the house, surprised by the stranger. One whispered, \"Maybe she's a kind fairy?\"",
    [CASE_TXT_10] = "Snow White waited for someone to return. The forest grew dark, and an owl hooted softly. Suddenly, laughter and footsteps came from the forest.",
    [CASE_TXT_11] = "Snow White entered the house; it was warm and cozy. Photos hung on the wall, books were on a shelf. She whispered, \"Wow! What a neat person must live in here!\"",
    [CASE_TXT_12] = "Snow White knocked. At first it was dead silence, then she heard quiet footsteps. \"Who's there?\" a childlike voice asked.",
    [CASE_TXT_13] = "She saw seven little dwarves having dinner after work. They seemed kind and cheerful. Snow White felt that she would be happy here.",
    [CASE_TXT_14] = "Snow White smiled politely and said, \"Good evening, grandmother.\" The old woman held out an apple to her and whispered, \"Take it, it's magical.\" But something quivered in the air - the forest seemed to freeze.",
    [CASE_TXT_15] = "Snow White hid and watched. The old woman looked around and disappeared into the darkness.",
    [CASE_TXT_16] = "When the dwarves returned, the house smelled of vanilla and honey, and a stranger named Snow White was waiting for them at the table.\n"
                    "She asked for shelter, and the dwarves gladly agreed.\n"
                    "From then on, Snow White kept their house, and in the evenings they all sang songs together, making the forest feel brighter.",
    [CASE_TXT_17] = "The dwarves were delighted by the unexpected celebration and immediately offered Snow White their friendship. After hearing her sad story, they took her to an empty castle, where all the inhabitants immediately loved the kind princess. In gratitude, Snow White threw a real feast for her little friends, and their friendship lasted for many years.",
    [CASE_TXT_18] = "After hearing Snow White's story, the dwarves learned that the evil mirror had enchanted the Queen. They immediately smashed it, and the magic dissipated. The repentant Queen asked for forgiveness, and the kind-hearted Snow White forgave her. From then on, the castle was filled not with orders, but with love and true happiness.",
    [CASE_TXT_19] = "The dwarfs did not wake the sleeping girl but left her breakfast by the bed. When she awoke, Snow White smiled, realizing that kindness had found her even in the deepest forest. And from that day on, the little house became her home - a place of peace and quiet joy.",
    [CASE_TXT_20] = "Snow White stepped forward bravely: \"My name is Snow White. I'm lost in the forest.\" The prince smiled: \"I've been looking for you for several days. The King is worried!\" He took her home, where her father embraced his daughter, overcome with joy.",
    [CASE_TXT_21] = "Snow White hid, but an owl suddenly hooted loudly, and the dwarves turned around. \"Look, someone's there!\" one shouted. Snow White came out, smiling sheepishly. It turned out the dwarves were enchanted and waiting for a princess to break the spell. The eldest brother married his savior, and their lives were filled with happiness in a distant kingdom.",
    [CASE_TXT_22] = "When Snow White finished decorating the house, the mirror came to life. Her kind heart was rewarded - the magic mirror brought her father back. It turned out the evil Queen was imprisoned forever in the mirror for her envy. Snow White and the King were reunited and lived happily ever after in the castle.",
    [CASE_TXT_23] = "She waited quietly, listening to the crackle of the fire. When dawn came, the room filled with the scent of flowers, and green shoots rose from the floor like living light. The forest had given her a gift - a garden to call her home.",
    [CASE_TXT_24] = "Snow White knocked gently and said, \"It's me - I mean no harm.\" The door opened, and a kind young prince smiled and invited her inside. He listened to her story, and before the night was over, he had already fallen in love.",
    [CASE_TXT_25] = "At sunrise, the forest turned into light and song. The house she feared became her father's garden, and the deer she trusted turned into a young spirit who smiled and vanished. Snow White stepped forward - no longer lost, but finally home.",
    [CASE_TXT_26] = "Snow White knocked - and the door opened. \"Come in, dear, you're like a ray of sunshine in our home,\" said one dwarf. The kind dwarves turned out to be stars from the Little Dipper; they gave Snow White a magic ring as a farewell gift. It could transport her anywhere in an instant. With this gift, the dark forest no longer frightened the girl, for a whole life of adventure lay ahead.",
    [CASE_TXT_27] = "Snow White did not enter - it was enough for her to see the light and hear the laughter. But then came the clatter of hooves and the sound of a hunting horn; a black stallion with a rider in dark robes jumped onto the path. It was the Night Prince, the ruler of the dark forest. He took Snow White away with him. However, the stern ruler quickly succumbed to the charm of the kind and cheerful girl. Soon they became inseparable companions on their nightly journeys.",
    [CASE_TXT_28] = "Snow White recognized the Queen and accepted her gift with kindness, thus breaking the evil spell. The touched Queen repented and offered to grow a whole orchard instead of the poisoned apple. Snow White forgave her, still she hid the apple in a casket as a reminder of the wonderful reconciliation.",
    [CASE_TXT_29] = "Snow White refused the magic, and the old woman turned out to be a kind fairy. As a reward for her honesty, the fairy left her a magic flower. Snow White planted it, and by morning a garden and a cottage had grown, where she began helping lost travelers find their way home.",
    [CASE_TXT_30] = "Snow White followed the trail until the old woman led her back to the castle. There she turned into the Queen, and Snow White realized that her stepmother was a witch. Long ago, her own mother had said that against dark magic there was only one remedy - a song coming from the heart. Casting aside fear and doubt, Snow White emerged from her hiding place and began a lovely song. At first her voice trembled, but then skylarks and nightingales joined in. Soon the whole area rang with a pure melody. And the Queen and her evil mirror vanished, as if they had never existed.",
    [CASE_TXT_31] = "At dawn, the forest awoke to gentle light and the song of birds. By the shining waterfall, Snow White met her father, who had searched for her through the night. They embraced, and the morning sun rose - warm, bright, and full of peace.",
};

static int clamp_index(int idx)
{
    if (idx < 0) {
        return 0;
    }
    if (idx >= CASE_TXT_COUNT) {
        return CASE_TXT_COUNT - 1;
    }
    return idx;
}

int builtin_reading_text_count(void)
{
    return (int)(sizeof(kReadingTexts) / sizeof(kReadingTexts[0]));
}

const char *builtin_get_reading_text_for_case(builtin_text_case_t c)
{
    const char *text = kReadingTexts[clamp_index((int)c)];
    return text ? text : "";
}

const char *builtin_get_reading_text(void)
{
    return builtin_get_reading_text_for_case(builtin_text_get());
}

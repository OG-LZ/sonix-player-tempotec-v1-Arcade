/* The Black Testament: an original gothic cosmogony told by conflicting relics,
 * witness fragments, and the broken history of Thren. GPL-3.0-or-later.
 * All narrative text is immutable static storage. No runtime allocation.
 * Do not extend dg_game or touch the procedural RNG / save ABI. */
#include "dungeon_lore.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned floor;
    uint8_t required_relic;
    dg_lore_fragment passage;
} lore_unlock;

/* A death is not a failure of darkness; it is a right the Witness stole. */
static const lore_unlock TESTAMENT[] = {
    {1, 0, {"BEFORE THE BELLS",
        "In elder Thren, darkness\n"
        "kept the dead by name.\n"
        "No grave was ever empty.\n"
        "No spirit had to wander."}},
    {1, 0, {"THE MERCIFUL DARK",
        "The stars were not born.\n"
        "They were wounds, cut in\n"
        "night by those who feared\n"
        "the kindness of an end."}},
    {1, 0, {"THE FIRST LANTERN",
        "Queen Sereth bade them\n"
        "make a light that could\n"
        "remember what flesh\n"
        "and stone could not."}},
    {1, 0, {"THE NINE HOUSES",
        "Nine houses swore to\n"
        "guard the royal flame.\n"
        "Eight gave their names.\n"
        "The ninth gave its child."}},
    {3, 0, {"THE LICHEN CHOIR",
        "The healers buried names\n"
        "in the roots of a mold.\n"
        "Now the caverns sing\n"
        "in borrowed voices."}},
    {4, 0, {"ORVAIN'S CROWN",
        "King Orvain built a\n"
        "throne of unburied sons.\n"
        "Each bone bears a name\n"
        "he could not release."}},
    {5, 0, {"THE WHITE HOURS",
        "Lady Caldris froze\n"
        "her dying son's last\n"
        "breath. Winter has held\n"
        "it for a thousand years."}},
    {7, 0, {"THE RED LITANY",
        "Bishop Vaust counted\n"
        "the unremembered dead.\n"
        "His ledger filled;\n"
        "his altar never did."}},
    {8, 0, {"ILYR, THE WARDEN",
        "Ilyr locked the gates\n"
        "to death in mercy.\n"
        "He could not bear\n"
        "one more farewell."}},
    {9, 0, {"THE ABYSS FORGE",
        "Morae Ninehands forged\n"
        "a wick from the bells.\n"
        "It burned not with oil,\n"
        "but with what we were."}},
    {11, 0, {"THE RELIC SANCTUM",
        "Below the last altar\n"
        "lies the Name Engine.\n"
        "Each floor is a year\n"
        "the world forgot."}},
    {12, 0, {"VAUST'S CONFESSION",
        "I fed the Light the\n"
        "names of the living.\n"
        "It remembered them.\n"
        "The world did not."}},
    {13, 0, {"BELOW THE TWELFTH",
        "There was never a\n"
        "bottom to this place.\n"
        "We descend through\n"
        "all the unwritten years."}},
    {25, 0, {"THE KNIGHT'S SIN",
        "You carried a child\n"
        "away from the dawn.\n"
        "You know her name.\n"
        "The Lantern ate it."}},
    {49, 0, {"THE NINTH HOUSE",
        "There were no heirs.\n"
        "You were the ninth\n"
        "house: oath and ember,\n"
        "given a human face."}},
    {1, DG_RELIC_BLOOD, {"THE BLOOD IDOL",
        "Saint Merrow healed\n"
        "the dying with lives\n"
        "not yet lived. Her\n"
        "hands never grew old."}},
    {1, DG_RELIC_MIRROR, {"THE MIRROR FANG",
        "Orvain hid the face\n"
        "of his firstborn in\n"
        "polished silver. It\n"
        "still refuses to die."}},
    {1, DG_RELIC_EMBER, {"THE EMBER TORCH",
        "This torch preceded\n"
        "the counterfeit dawn.\n"
        "The bearer cast no\n"
        "shadow thereafter."}},
    {1, DG_RELIC_GHOST, {"THE GHOST BELL",
        "Eight bells rang as\n"
        "Thren was erased.\n"
        "The ninth is heard\n"
        "only by the dying."}},
    {1, DG_RELIC_FROST, {"THE FROST HEART",
        "Caldris caught a last\n"
        "heartbeat in ice.\n"
        "When it beats again,\n"
        "the city must thaw."}},
    {1, DG_RELIC_THORN, {"THE THORN CROWN",
        "The sister Avara\n"
        "wore these thorns to\n"
        "prove that suffering\n"
        "was still her own."}},
    {1, DG_RELIC_MOON, {"THE MOON COIN",
        "No king is stamped\n"
        "upon this silver.\n"
        "The face belongs to\n"
        "the last tax collector."}},
    {1, DG_RELIC_STONE, {"THE STONE SIGIL",
        "Wardens carved guilt\n"
        "into their own flesh.\n"
        "The stones grew heavy.\n"
        "The sins remained."}},
};

static const dg_lore_fragment ROOM_ECHO[9][3] = {
    { {"THE MARCH OF BONES", "The dead still march\nwhere no war was won.\nThey obey a crown\nthat remembers nobody."},
      {"AN EMPTY PROCESSION", "A thousand helms face\nthe same bare wall.\nThey await the order\nto stop remembering."},
      {"A SOLDIER'S PRAYER", "Beneath each tongue\na royal command is\ncarved in bone:\nDo not lay me down."} },
    { {"THE IRON TRIAL", "The executioners\nwere granted mercy.\nThey may kill forever;\nthey may never sleep."},
      {"THE BLACK OATH", "The iron was cold\nbefore it was forged.\nEvery blade bears\nthe mark of a king."},
      {"THE LAST CHAMPION", "He offered his heart\nfor the city's safety.\nThey returned his heart\nand kept his name."} },
    { {"THE FALSE GIFT", "Gold belongs to those\nwho still possess a\nname. The vault knows\nwhat yours once was."},
      {"THE GILDED TOMB", "Here the rich buried\ntheir unlived years.\nEvery coin has been\npaid for with a day."},
      {"THE SEALED BARGAIN", "The treasure opens.\nThe room listens.\nA choice is a prayer\nthat can be answered."} },
    { {"WELL OF CINDERS", "No water rises here.\nIt is the weeping\nof lamps that have\nburned a name away."},
      {"THE LAST SPRING", "The healers hid\ntheir names in water.\nDrink, and it will\ntry to learn yours."},
      {"THE KIND WELL", "It has healed men\nwho later forgot\ntheir mothers. It\nasks no other fee."} },
    { {"ALTAR OF ASH", "The saints had no\nfaces on the day\nof the first ringing.\nThe icons still do."},
      {"THE BROKEN HALO", "A crown is merely\na chain placed high.\nBow if you will.\nIt will not bow."},
      {"THE RELIC'S OATH", "A holy thing is\na wound preserved\nuntil someone calls\nits pain a miracle."} },
    { {"MORAE'S ANVIL", "Her hammers sleep.\nThe fire has never\nceased its counting.\nWhat will you trade?"},
      {"THE LAST SMITH", "Morae has nine hands.\nEight shape the steel.\nOne measures what\nyou came to forget."},
      {"AN UNPAID DEBT", "The forge accepts\ncoin and sacrifice.\nIt cannot tell\nthe difference now."} },
    { {"THE RUNE'S LAW", "The stones do not\nblock the passage.\nThey keep a buried\nname from escaping."},
      {"THE SILENT TEST", "The puzzle was made\nfor a child who\nnever returned.\nDo not move gently."},
      {"A LETTER IN STONE", "No alphabet lives\nin these markings.\nThey are footsteps\nof the lost."} },
    { {"THE SOVEREIGN'S LAW", "A king has three\nduties: to witness,\nto remember, and\nat last to release."},
      {"THE EMPTY THRONE", "Three sovereigns\nchose to remain.\nTheir kingdoms died.\nThey called it love."},
      {"THE LAST VERDICT", "When the bells are\nheard beneath the\nworld, no throne\ncan judge the dead."} },
    { {"THE FIRST DESCENT", "The stair was made\nto lead men home.\nIt now remembers\nonly downward."},
      {"LANTERN'S REST", "Here a knight once\nremoved his helm.\nNo face looked back\nfrom the darkness."},
      {"A LIGHT REMAINS", "Someone was waiting\nat the first step.\nHer name is lost.\nThe waiting is not."} },
};

static const char *const WHISPERS[9][3] = {
    {"The bones remember war.","The procession will not end.","No soldier knows his name."},
    {"Iron thirsts for mercy.","The execution never ends.","The last oath is unkept."},
    {"Gold counts stolen years.","The seal has heard you.","Someone paid for this."},
    {"The water remembers grief.","Drink, and be known.","A thousand lamps weep."},
    {"The saints have no faces.","The relic hears your oath.","Prayers rot slower than flesh."},
    {"The anvil counts debts.","Nine hammers are silent.","The forge keeps no names."},
    {"Stone remembers footsteps.","Something sleeps in the rune.","The wall will not confess."},
    {"A sovereign holds the dark.","The throne awaits a verdict.","The bells have no master."},
    {"The stair remembers down.","The child never came back.","The first oath endures."},
};

static const char *const OMENS[3] = {
    "I AM ORVAIN, LAST KING.\nMy sons are my crown.\nI will not bury them.\nYou shall not pass.",
    "I AM ILYR, WARDEN.\nI shut the gate to death.\nYou call it a prison.\nI call it mercy.",
    "I AM VAUST, BISHOP.\nEvery name is a wick.\nThe world shall remember\neven if none survive."
};

/* Choose echoes without touching the combat RNG or altering a room's seed. */
static unsigned room_variant(const dg_game *game) {
    return (unsigned)(dg_hash(game->floor_seed ^
        ((uint32_t)game->current * UINT32_C(0x6d2b79f5))) % 3u);
}

unsigned dg_lore_count(const dg_game *game) {
    unsigned count = 0;
    if (!game || game->current >= game->count || game->depth == 0) return 0;
    count++; /* Present room is always page zero. */
    for (size_t i = 0; i < sizeof TESTAMENT / sizeof TESTAMENT[0]; i++) {
        if (game->depth >= TESTAMENT[i].floor &&
            (!TESTAMENT[i].required_relic ||
             (game->relics & TESTAMENT[i].required_relic))) {
            count++;
        }
    }
    return count;
}

bool dg_lore_at(const dg_game *game, unsigned index, dg_lore_fragment *out) {
    if (!out || !game || game->current >= game->count || game->depth == 0) return false;
    unsigned kind = game->rooms[game->current].kind;
    if (index == 0) {
        if (kind > DG_ENTRANCE) return false;
        *out = ROOM_ECHO[kind][room_variant(game)];
        return true;
    }
    unsigned page = 1;
    for (size_t i = 0; i < sizeof TESTAMENT / sizeof TESTAMENT[0]; i++) {
        if (game->depth < TESTAMENT[i].floor ||
            (TESTAMENT[i].required_relic &&
             !(game->relics & TESTAMENT[i].required_relic))) continue;
        if (page++ == index) {
            *out = TESTAMENT[i].passage;
            return true;
        }
    }
    return false;
}

const char *dg_lore_room_whisper(const dg_game *game) {
    if (!game || game->current >= game->count || game->depth == 0)
        return "The lantern is silent.";
    unsigned kind = game->rooms[game->current].kind;
    if (kind > DG_ENTRANCE) return "The past is lost.";
    return WHISPERS[kind][room_variant(game)];
}

const char *dg_lore_boss_omen(const dg_game *game) {
    if (!game || game->current >= game->count ||
        game->rooms[game->current].kind != DG_BOSS) return "The throne is empty.";
    unsigned v = game->rooms[game->current].boss_variant;
    return OMENS[v < 3 ? v : 0];
}

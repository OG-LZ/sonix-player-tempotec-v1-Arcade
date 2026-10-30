/* Pocket Dungeon campaign presentation. Original text, allocation-free.
 * Kept independent of LVGL, game state and audio transport. GPL-3.0-or-later. */
#ifndef DUNGEON_CAMPAIGN_PRESENTATION_H
#define DUNGEON_CAMPAIGN_PRESENTATION_H

typedef struct { const char *title; const char *body; } dg_campaign_page;
#define DG_CAMPAIGN_EPILOGUE_PAGES 4u
static inline const dg_campaign_page *dg_campaign_arrival(unsigned depth){
    static const dg_campaign_page arrivals[]={
        {"THE UNWRITTEN","The ledger has no place for you. Follow the lantern."},
        {"THE HOUSE OF ORESSA","Every name is a number. Yours is missing."},
        {"THE HOUSE OF VHARN","The stones remember the builders they buried."},
        {"THE HOUSE OF SIBRELL","A bell can carry a warning or bury one."},
        {"THE HOUSE OF KETHIS","The lanterns hold more than flame."},
        {"THE HOUSE OF CORVANE","Mercy is a debt someone else must pay."},
        {"THE FALLING CHOIR","They fall from a history that never happened."},
        {"THE THOUSAND QUIET HOURS","Six weeks of bells. A generation without sound."},
        {"THE ASHEN BREACH","The last bell waits beneath the broken vault."},
        {"THE TESTAMENT'S JUDGMENT","Melira waits beside the book that edits worlds."}
    };
    return depth<sizeof(arrivals)/sizeof(arrivals[0])?&arrivals[depth]:0;
}
static inline const dg_campaign_page *dg_campaign_ending(unsigned ending,unsigned page){
    static const dg_campaign_page endings[3][DG_CAMPAIGN_EPILOGUE_PAGES]={
        {
            {"THE NINTH EDIT","Melira: 'I can save them. Not all. Never all.'\nThe Unwritten returns the pen."},
            {"THE PRICE OF MORNING","The black sun rises. Families wake to a world that holds together. In the margins, the erased remain unnamed."},
            {"THE WITNESS REMAINS","The Witness remembers every voice the Testament excludes. It does not forgive. It does not forget."},
            {"THE END / THE NINTH EDIT","The world survives another morning.\nThe cost survives with it.\n\nTHE BLACK TESTAMENT\nA Pocket Dungeon story\nRETURN TO ARCADE to finish."}
        },{
            {"THE UNMAKING","Melira: 'Without the book, I cannot promise tomorrow.'\nThe Unwritten breaks the binding."},
            {"THE RETURN OF THE LOST","Streets overlap. Graves open onto gardens. Those erased return to a world that cannot agree which century it is."},
            {"A WORLD WITHOUT A PEN","The Witness falls silent. There is no editor left to blame, and no single history left to obey."},
            {"THE END / THE UNMAKING","No one is promised safety.\nNo one may be quietly removed.\n\nTHE BLACK TESTAMENT\nA Pocket Dungeon story\nRETURN TO ARCADE to finish."}
        },{
            {"THE TENTH WITNESS","The Choir speaks its names. The Unwritten does not choose for them. Melira lays down the pen."},
            {"THE RIGHT TO REFUSE","The erased are offered a place, not assigned one. Some return. Some decline. Both answers are entered without correction."},
            {"THE FIRST UNEDITED DAWN","The Witness becomes a chorus rather than a judge. Candlewake opens its doors to the histories that survived."},
            {"THE END / TENTH WITNESS","No single hand owns the record.\nNo mercy without consent.\n\nTHE BLACK TESTAMENT\nA Pocket Dungeon story\nRETURN TO ARCADE to finish."}
        }
    };
    return ending>=1&&ending<=3&&page<DG_CAMPAIGN_EPILOGUE_PAGES?&endings[ending-1][page]:0;
}
#endif

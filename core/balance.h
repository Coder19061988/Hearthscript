#pragma once

#include <map>
#include <string>
#include <vector>

namespace kindle {

// Something a workshop makes ("recipes" in config/balance.json): per unit it takes `inputs` from the worker's
// hands and gives one item — a resource into the hands, or a tool into the tool slot.
struct Recipe {
    std::string at;         // where: "house" or a workshop type ("smithy")
    std::string workplace;  // one worker at a time per building: "mill", "oven", "forge"
    std::map<std::string, int> inputs;
    double minutes = 0;     // per unit
    bool fire = false;      // the building's stove must burn
    bool heavy = false;     // heavy work (tires like chopping)
};

// A kind of workshop ("workshops" in config/balance.json): its name and what its blueprint needs.
struct WorkshopType {
    std::string name;                       // shown to the player: "Кузня"
    std::map<std::string, int> materials;  // brought to the construction site
    double hours = 0;                       // of construction work
    bool stove = false;                     // heated like a house (the hospital): me.heat, temperature inside
};

// Tunable numbers of the prototype. Defaults mirror "Баланс_прототипу.md";
// config/balance.json may override any subset of them (same key names).
struct Balance {
    // Time
    double day_real_minutes = 12.0;          // real minutes per game day at x1 (for the UI layer)
    double night_start_hour = 21.0;
    double night_end_hour = 5.0;
    double think_seconds_per_line = 2.0;     // "thinking" cost of every executed line of player code
    double idle_log_hours = 4.0;             // log a warning after this long without any action
    double time_of_day_grace_minutes = 10.0; // until=/wait_until(time) that passed less than this ago counts as "now"

    // Movement and load
    double walk_meters_per_minute = 60.0;
    double walk_loaded_meters_per_minute = 45.0;  // when carrying more than half of the limit
    double weight_limit_kg = 50.0;
    double wood_kg = 5.0;
    double bread_kg = 0.5;

    // Action durations, game minutes
    double tool_take_minutes = 1.0;          // take / put_back
    double tool_wear_per_hour = 2.5;         // % of condition per hour of work (chop / harvest); ~4 days of 10 h
    double tool_worn_warning = 25.0;         // the log warns once the condition drops below this, %
    // Tools are not repaired: any tool can be dismantled into scrap (and wood from a handle)
    double dismantle_minutes = 10.0;
    double axe_scrap = 2.0;
    double scythe_scrap = 2.0;
    double sickle_scrap = 1.0;
    double dismantle_wood = 1.0;             // an axe's or a scythe's handle
    double scrap_kg = 1.0;
    double transfer_base_minutes = 1.0;      // pick_up / drop
    double transfer_per_unit_minutes = 0.2;
    double chop_minutes_per_unit = 20.0;
    double eat_minutes = 15.0;
    double wood_per_tree = 5.0;

    // Hunger
    double hunger_per_hour = 4.0;
    double bread_hunger_relief = 40.0;
    double hunger_damage_threshold = 80.0;
    double hunger_damage_per_hour = 1.0;
    double hunger_full_damage_per_hour = 1.5;  // at hunger == 100

    // Fatigue, per hour (negative values restore)
    double fatigue_heavy_per_hour = 6.0;
    double fatigue_light_per_hour = 3.0;
    double fatigue_rest_per_hour = -8.0;
    double fatigue_sleep_home_per_hour = -12.0;
    double fatigue_sleep_ground_per_hour = -6.0;
    double fatigue_slow_threshold = 90.0;
    double fatigue_slow_factor = 0.5;
    double fatigue_full_damage_per_hour = 1.0;  // at fatigue == 100

    // Health
    double health_regen_per_hour = 1.0;
    double health_regen_max_hunger = 50.0;
    double health_regen_max_fatigue = 70.0;

    // Rain
    double rain_chance_per_day = 0.3;
    double rain_min_hours = 1.0;
    double rain_max_hours = 3.0;
    double rain_earliest_start_hour = 6.0;
    double rain_latest_start_hour = 20.0;
    double rain_work_factor = 0.7;
    double rain_extra_fatigue_per_hour = 2.0;

    // Seasons and temperature (MVP: warm season + winter; it rains only in the warm season)
    double season_warm_days = 10.0;
    double season_winter_days = 5.0;
    double temp_warm_avg = 16.0;          // °C, daily average
    double temp_winter_avg = -15.0;
    double temp_day_amplitude = 5.0;      // coldest at 02:00, warmest at 14:00
    double temp_rain_drop = 3.0;

    // Warmth of a character (0..100)
    double comfort_temperature = 10.0;    // perceived temperature at which warmth stops dropping
    double warmth_recover_per_hour = 10.0;
    double warmth_loss_per_degree_hour = 0.8;
    double clothes_bonus = 5.0;           // regular clothes add this to the perceived temperature
    double warm_clothes_bonus = 20.0;
    double bed_bonus = 10.0;              // sleeping in a bed at home (instead of clothes)
    double work_heat_bonus = 5.0;         // heavy work warms up
    double warmth_damage_threshold = 30.0;
    double warmth_damage_per_hour = 1.0;
    double warmth_zero_damage_per_hour = 2.0;
    double health_regen_min_warmth = 50.0;
    double wear_minutes = 5.0;

    // House: stove (heating + baking), chest, hand mill
    double house_heated_temperature = 20.0;
    double house_shelter_bonus = 5.0;     // an unheated house is this much warmer than outside
    double house_warmup_hours = 1.0;      // time constants of the inside temperature
    double house_cooldown_hours = 4.0;
    double firewood_burn_hours = 2.0;     // one unit of wood in the stove burns this long

    // Field and food chain: field -> grain -> hand mill -> flour -> oven -> bread
    double field_seed_grain = 10.0;
    double field_sow_hours = 3.0;
    double field_grow_days = 4.0;         // growing happens only in the warm season
    double field_yield_grain = 80.0;
    double field_harvest_hours = 6.0;
    double grain_kg = 0.5;
    double flour_kg = 0.5;

    // Construction from blueprints, moving in
    double house_capacity = 4.0;
    double house_build_wood = 40.0;
    double house_build_hours = 16.0;
    double storage_build_wood = 30.0;
    double storage_build_hours = 8.0;
    double field_build_hours = 4.0;        // ploughing, no materials
    double move_in_minutes = 10.0;
    double blueprint_min_distance = 25.0;  // meters to other buildings (forest: + its radius)

    // Animals (cows and chickens live in a barn), the meadow and hay
    double animal_hunger_per_hour = 2.5;         // in winter: they live on stored feed
    double animal_graze_per_hour = 5.0;          // warm season: grazing lowers hunger
    double animal_hunger_damage_threshold = 80.0;
    double animal_hunger_damage_per_hour = 1.5;
    double animal_health_regen_per_hour = 1.0;
    double animal_produce_max_hunger = 50.0;     // milk and eggs only while fed
    double cow_hay_relief = 10.0;                // hunger per unit of hay
    double chicken_grain_relief = 60.0;          // hunger per unit of grain
    double cow_milk_per_day = 4.0;
    double cow_milk_max = 8.0;
    double chicken_eggs_per_day = 1.0;
    double chicken_eggs_max = 3.0;
    double cow_meat = 20.0;
    double chicken_meat = 2.0;
    double slaughter_minutes_per_meat = 4.0;
    double milk_minutes_per_unit = 5.0;
    double meadow_hay_max = 60.0;
    double meadow_regrow_days = 4.0;             // from empty to full, warm season only
    double mow_minutes_per_unit = 10.0;
    double barn_build_wood = 30.0;
    double barn_build_hours = 8.0;
    double barn_capacity = 8.0;
    double hay_kg = 2.0;
    double milk_kg = 1.0;
    double eggs_kg = 0.1;
    double meat_kg = 1.0;
    double meat_hunger_relief = 30.0;
    double milk_hunger_relief = 15.0;
    double eggs_hunger_relief = 10.0;

    // Trade: the merchant, the trading post, coins in the town hall; values of goods
    double start_coins = 50.0;  // coins in the town hall at the start
    double merchant_first_day = 5.0;  // the first visit (day number)
    double merchant_interval_days = 5.0;
    double merchant_arrive_hour = 8.0;
    double merchant_leave_hour = 20.0;  // he trades when he leaves
    double merchant_price_spread = 0.25;  // prices are value * (1 +- spread) per visit
    double merchant_offer_share = 0.5;  // he pays this share of the price
    double trading_post_build_wood = 20.0;
    double trading_post_build_hours = 6.0;
    double order_minutes = 2.0;  // me.order
    double clothes_kg = 2.0;  // a set of clothes in hands
    double value_wood = 4.0;  // base value of goods, coins per unit
    double value_grain = 6.0;
    double value_flour = 8.0;
    double value_bread = 10.0;
    double value_hay = 3.0;
    double value_milk = 6.0;
    double value_eggs = 3.0;
    double value_meat = 12.0;
    double value_clothes = 30.0;
    double value_warm_clothes = 60.0;
    double value_axe = 50.0;
    double value_sickle = 40.0;
    double value_scythe = 50.0;
    double value_chicken = 25.0;
    double value_cow = 200.0;
    double value_scrap = 8.0;

    // Flax and clothes: a crop, cloth, garments that wear while worn and turn into rags
    double flax_seed = 10.0;  // flax to sow a field
    double flax_yield = 40.0;  // flax from a ripe field
    double flax_kg = 0.5;
    double cloth_kg = 1.0;
    double rags_kg = 0.5;
    double clothes_wear_per_day = 5.0;  // % of condition per day worn (~20 days)
    double warm_clothes_wear_per_day = 7.0;  // % per day worn (~14 days)
    double clothes_worn_warning = 25.0;  // the log warns below this, %
    double value_flax = 3.0;
    double value_cloth = 10.0;
    double value_rags = 1.0;

    // Wood: planks from the sawmill, beds from the carpentry; a bed makes sleep restore faster
    double planks_kg = 4.0;
    double bed_kg = 20.0;
    double fatigue_sleep_bed_per_hour = -16.0;  // sleeping in a bed (on the floor at home: -12)
    double value_planks = 4.0;
    double value_bed = 30.0;

    // Stone and metal: the quarry and the mine (a pickaxe), charcoal from the kiln, iron in the smithy
    double quarry_stone = 400.0;  // stone in the quarry at the start
    double mine_ore = 150.0;  // ore in the mine at the start
    double mine_stone_minutes = 15.0;  // a unit of stone with a pickaxe
    double mine_ore_minutes = 30.0;  // a unit of ore with a pickaxe
    double stone_kg = 10.0;
    double ore_kg = 8.0;
    double charcoal_kg = 2.0;
    double iron_kg = 5.0;
    double pickaxe_scrap = 2.0;  // dismantling a pickaxe
    double value_stone = 2.0;
    double value_ore = 4.0;
    double value_charcoal = 3.0;
    double value_iron = 12.0;
    double value_pickaxe = 45.0;

    // Water: the well, watering a field speeds its growth (rain waters the fields too)
    double water_kg = 10.0;  // a bucket
    double field_water_units = 5.0;  // buckets to water a field
    double field_water_minutes = 30.0;  // watering a field
    double field_water_days = 1.0;  // watering lasts
    double field_water_growth_bonus = 0.5;  // a watered field grows this much faster
    double field_rain_soak_hours = 6.0;  // rain waters the fields for this long after it

    // Health: illness of the weakened, injuries at hard work, the hospital, herbs and bandages
    double illness_hours = 72.0;  // an untreated illness lasts
    double illness_treated_hours = 12.0;  // after treatment it ends in at most this
    double illness_damage_per_hour = 1.0;  // health lost while sick and up (untreated)
    double illness_rest_damage_per_hour = 0.2;  // ... while resting or sleeping
    double illness_fatigue_per_hour = 2.0;  // extra fatigue while sick and up (untreated)
    double illness_work_factor = 0.7;  // work speed while sick (untreated)
    double illness_cold_per_hour = 0.03;  // chance per hour to fall ill while freezing (warmth below warmth_damage_threshold)
    double illness_hunger_per_hour = 0.01;  // ... while starving (hunger >= hunger_damage_threshold)
    double illness_exhaustion_per_hour = 0.01;  // ... while exhausted (fatigue >= fatigue_slow_threshold)
    double illness_rain_per_hour = 0.003;  // ... while in the rain without a roof
    double injury_hours = 120.0;  // an untreated injury heals in
    double injury_treated_hours = 48.0;  // a bandaged one in at most this
    double injury_work_factor = 0.5;  // work speed while injured
    double injury_fatigue_threshold = 80.0;  // heavy work above this fatigue risks an injury
    double injury_fatigue_per_hour = 0.02;  // chance per hour of heavy work when tired
    double injury_dark_per_hour = 0.02;  // ... at night
    double injury_mine_per_hour = 0.004;  // ... in the quarry or the mine
    double injury_forge_per_hour = 0.003;  // ... at the forge
    double herbs_minutes = 30.0;  // gathering a unit of herbs on a meadow (warm season)
    double herbs_kg = 0.2;
    double bandage_kg = 0.1;
    double treat_minutes = 30.0;
    double treat_herbs = 2.0;  // herbs to treat an illness (an injury takes a bandage)
    double value_herbs = 2.0;
    double value_bandage = 3.0;

    // Mood: work and free time, needs, comfort, unburied bodies; the church: services and burials
    double mood_start = 80.0;  // at the start of the game
    double mood_heavy_per_hour = -0.6;  // mood per hour of heavy work
    double mood_light_per_hour = -0.3;  // ... of light work and walking
    double mood_rest_per_hour = 3.0;  // ... of free time (me.rest)
    double mood_bed_per_hour = 0.5;  // ... of sleep in a bed (comfort)
    double mood_ground_per_hour = -1.0;  // ... of sleep outside
    double mood_hunger_per_hour = -2.0;  // while starving (hunger >= hunger_damage_threshold)
    double mood_cold_per_hour = -2.0;  // while freezing (warmth below warmth_damage_threshold)
    double mood_exhaustion_per_hour = -1.0;  // while exhausted (fatigue >= fatigue_slow_threshold)
    double mood_ailment_per_hour = -1.0;  // while ill or injured
    double mood_body_per_hour = -1.0;  // for every decaying unburied body in the settlement
    double mood_low_threshold = 30.0;  // below this the work is slower
    double mood_low_work_factor = 0.75;
    double body_decay_hours = 24.0;  // an unburied body starts to decay after this
    double illness_body_per_hour = 0.01;  // chance per hour to fall ill for every decaying body
    double service_minutes = 60.0;  // a service in the church
    double service_mood = 20.0;  // mood for everyone at the church when it ends
    double service_interval_hours = 20.0;  // a church holds a service at most this often
    double burial_minutes = 60.0;  // at the cemetery, after bringing the body

    // Light: lanterns lit with oil every evening and torches; heavy work in the dark risks injuries
    double lantern_build_wood = 3.0;
    double lantern_build_hours = 1.0;
    double lantern_radius_m = 40.0;  // a lit lantern lights the work around it at night
    double lantern_oil_hours = 4.0;  // a unit of oil burns this long
    double torch_hours = 2.0;  // a torch in the hands burns this long of heavy work at night
    double light_minutes = 5.0;  // filling and lighting a lantern
    double oil_kg = 1.0;
    double torch_kg = 0.5;
    double value_oil = 4.0;
    double value_torch = 2.0;

    // Food: perishables spoil in storages (slower in winter and in a cellar); smoked meat and cheese keep; the diet
    double bread_spoil_per_day = 0.03;  // share of the stock that spoils a day in the warm season
    double milk_spoil_per_day = 0.5;
    double eggs_spoil_per_day = 0.1;
    double meat_spoil_per_day = 0.25;
    double smoked_meat_spoil_per_day = 0.01;
    double cheese_spoil_per_day = 0.01;
    double winter_spoil_factor = 0.3;  // in winter food spoils this much slower
    double cellar_spoil_factor = 0.3;  // in a cellar too (both together in a cellar in winter)
    double cellar_build_wood = 15.0;
    double cellar_build_stone = 10.0;
    double cellar_build_hours = 8.0;
    double smoked_meat_kg = 0.8;
    double cheese_kg = 1.0;
    double smoked_meat_hunger_relief = 30.0;
    double cheese_hunger_relief = 20.0;
    double value_smoked_meat = 10.0;
    double value_cheese = 8.0;
    double diet_days = 7.0;  // the diet is what a character ate over this many days
    double diet_variety_kinds = 3.0;  // this many kinds of food is a varied diet
    double diet_variety_mood_per_hour = 0.1;
    double diet_monotony_mood_per_hour = -0.1;  // one kind of food only (after the first diet_days of the game)

    // Forestry: saplings from the nursery are planted where trees were cut and grow into trees
    double sapling_kg = 1.0;
    double plant_minutes = 10.0;  // planting a sapling
    double tree_grow_days = 20.0;  // a sapling becomes a tree after this many warm-season days (~2 years)
    double value_sapling = 1.0;

    // Leisure: the bathhouse (fatigue, mood, fewer illnesses), beer from the brewery, the tavern (company, hangovers)
    double bath_minutes = 60.0;  // a bath (counts as rest)
    double bath_water = 2.0;  // buckets from the hands
    double bath_fatigue = 30.0;  // fatigue a bath takes away
    double bath_mood = 10.0;
    double bath_clean_hours = 48.0;  // after a bath the chances to fall ill are lower this long
    double bath_illness_factor = 0.5;
    double bath_interval_hours = 20.0;  // a character bathes at most this often
    double beer_kg = 1.0;
    double beer_mood = 6.0;  // a mug at the tavern
    double beer_hunger_relief = 5.0;
    double drink_minutes = 20.0;  // a mug (counts as rest)
    double hangover_beers = 3.0;  // this mug in one day brings a hangover
    double hangover_hours = 12.0;
    double hangover_work_factor = 0.8;
    double injury_hangover_per_hour = 0.02;  // chance per hour of heavy work with a hangover
    double tavern_rest_mood_per_hour = 2.0;  // extra mood while resting at a tavern (company)
    double value_beer = 5.0;

    // Notice board (limited knowledge: characters share what they saw)
    double board_build_wood = 5.0;
    double board_build_hours = 1.0;
    double board_post_minutes_per_note = 1.0;  // me.post: per note written (at least one)
    double board_read_base_minutes = 1.0;      // me.read: looking at the board
    double board_read_per_note_minutes = 0.2;  // me.read: per note that was news

    // Loads a flat JSON object {"key": number, ...}; keys starting with "_" are comments.
    // Returns false and sets `error` when the file can't be read or parsed.
    // Unknown keys are reported in `warnings` and ignored.
    // Workshops and what they make: only from the JSON ("workshops", "recipes"), there are no defaults in code.
    // An item may have several recipes (variants): the first one whose inputs are in the hands is used.
    std::map<std::string, WorkshopType> workshops;
    std::map<std::string, std::vector<Recipe>> recipes;

    bool load_json_file(const std::string& path, std::string& error, std::vector<std::string>& warnings);
    bool load_json_text(const std::string& text, std::string& error, std::vector<std::string>& warnings);
};

}  // namespace kindle

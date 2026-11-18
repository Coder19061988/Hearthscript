#include "balance.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace kindle {

namespace {

struct Field {
    const char* key;
    double Balance::*ptr;
};

#define KINDLE_FIELD(name) {#name, &Balance::name}
const Field kFields[] = {
    KINDLE_FIELD(day_real_minutes),
    KINDLE_FIELD(night_start_hour),
    KINDLE_FIELD(night_end_hour),
    KINDLE_FIELD(think_seconds_per_line),
    KINDLE_FIELD(idle_log_hours),
    KINDLE_FIELD(time_of_day_grace_minutes),
    KINDLE_FIELD(walk_meters_per_minute),
    KINDLE_FIELD(walk_loaded_meters_per_minute),
    KINDLE_FIELD(weight_limit_kg),
    KINDLE_FIELD(wood_kg),
    KINDLE_FIELD(bread_kg),
    KINDLE_FIELD(tool_take_minutes),
    KINDLE_FIELD(transfer_base_minutes),
    KINDLE_FIELD(transfer_per_unit_minutes),
    KINDLE_FIELD(chop_minutes_per_unit),
    KINDLE_FIELD(eat_minutes),
    KINDLE_FIELD(wood_per_tree),
    KINDLE_FIELD(hunger_per_hour),
    KINDLE_FIELD(bread_hunger_relief),
    KINDLE_FIELD(hunger_damage_threshold),
    KINDLE_FIELD(hunger_damage_per_hour),
    KINDLE_FIELD(hunger_full_damage_per_hour),
    KINDLE_FIELD(fatigue_heavy_per_hour),
    KINDLE_FIELD(fatigue_light_per_hour),
    KINDLE_FIELD(fatigue_rest_per_hour),
    KINDLE_FIELD(fatigue_sleep_home_per_hour),
    KINDLE_FIELD(fatigue_sleep_ground_per_hour),
    KINDLE_FIELD(fatigue_slow_threshold),
    KINDLE_FIELD(fatigue_slow_factor),
    KINDLE_FIELD(fatigue_full_damage_per_hour),
    KINDLE_FIELD(health_regen_per_hour),
    KINDLE_FIELD(health_regen_max_hunger),
    KINDLE_FIELD(health_regen_max_fatigue),
    KINDLE_FIELD(rain_chance_per_day),
    KINDLE_FIELD(rain_min_hours),
    KINDLE_FIELD(rain_max_hours),
    KINDLE_FIELD(rain_earliest_start_hour),
    KINDLE_FIELD(rain_latest_start_hour),
    KINDLE_FIELD(rain_work_factor),
    KINDLE_FIELD(rain_extra_fatigue_per_hour),
};
#undef KINDLE_FIELD

// Minimal parser for a flat JSON object whose values are numbers or strings.
class FlatJson {
public:
    explicit FlatJson(const std::string& text) : s_(text) {}

    struct Item {
        std::string key;
        bool is_number = false;
        double number = 0;
    };

    bool parse(std::vector<Item>& out, std::string& error) {
        skip_ws();
        if (!eat('{')) return fail("очікується '{'", error);
        skip_ws();
        if (eat('}')) return true;
        for (;;) {
            Item item;
            skip_ws();
            if (!parse_string(item.key)) return fail("очікується назва параметра в лапках", error);
            skip_ws();
            if (!eat(':')) return fail("очікується ':'", error);
            skip_ws();
            if (peek() == '"') {
                std::string ignored;
                if (!parse_string(ignored)) return fail("незакритий рядок", error);
            } else {
                const char* begin = s_.c_str() + i_;
                char* end = nullptr;
                item.number = std::strtod(begin, &end);
                if (end == begin) return fail("очікується число", error);
                i_ += static_cast<size_t>(end - begin);
                item.is_number = true;
            }
            out.push_back(item);
            skip_ws();
            if (eat(',')) continue;
            if (eat('}')) break;
            return fail("очікується ',' або '}'", error);
        }
        skip_ws();
        if (i_ != s_.size()) return fail("зайві символи після '}'", error);
        return true;
    }

private:
    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    bool eat(char c) {
        if (peek() != c) return false;
        ++i_;
        return true;
    }
    void skip_ws() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    bool parse_string(std::string& out) {
        if (!eat('"')) return false;
        while (i_ < s_.size() && s_[i_] != '"') {
            if (s_[i_] == '\\' && i_ + 1 < s_.size()) ++i_;  // keep escaped char as-is
            out.push_back(s_[i_++]);
        }
        return eat('"');
    }
    bool fail(const char* what, std::string& error) const {
        int line = 1;
        for (size_t k = 0; k < i_ && k < s_.size(); ++k)
            if (s_[k] == '\n') ++line;
        std::ostringstream os;
        os << "рядок " << line << ": " << what;
        error = os.str();
        return false;
    }

    const std::string& s_;
    size_t i_ = 0;
};

}  // namespace

bool Balance::load_json_file(const std::string& path, std::string& error, std::vector<std::string>& warnings) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "не вдалося відкрити " + path;
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // UTF-8 BOM

    std::vector<FlatJson::Item> items;
    FlatJson parser(text);
    if (!parser.parse(items, error)) {
        error = path + ": " + error;
        return false;
    }
    for (const auto& item : items) {
        if (!item.key.empty() && item.key[0] == '_') continue;  // comment
        bool known = false;
        for (const auto& f : kFields) {
            if (item.key == f.key) {
                known = true;
                if (item.is_number)
                    this->*f.ptr = item.number;
                else
                    warnings.push_back("параметр \"" + item.key + "\" має бути числом");
                break;
            }
        }
        if (!known) warnings.push_back("невідомий параметр \"" + item.key + "\" (пропущено)");
    }
    return true;
}

}  // namespace kindle

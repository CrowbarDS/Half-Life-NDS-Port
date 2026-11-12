#include "hlds/BrushInfo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace hlds
{
    using hldata::RenderClass;

    namespace
    {
        int intKey(const Entity &entity, std::string_view key, int fallback = 0)
        {
            const std::string text(entity.value(key));
            return text.empty() ? fallback : std::atoi(text.c_str());
        }

        bool startsWith(const std::string &text, const char *prefix)
        {
            return text.rfind(prefix, 0) == 0;
        }

        /// Classes that are never seen and never in the way: volumes the game
        /// only tests against.
        bool isVolumeOnly(const std::string &cls)
        {
            static const char *const kClasses[] = {
                "func_ladder",      "func_monsterclip",  "func_mortar_field",
                "env_bubbles",      "func_tankcontrols",
                "func_traincontrols"};
            if (startsWith(cls, "trigger_"))
                return true;
            for (const char *name : kClasses)
            {
                if (cls == name)
                    return true;
            }
            return false;
        }

        /// Classes (and flags) that are drawn but can be walked through.
        bool isPassable(const std::string &cls, int spawnflags)
        {
            // (func_friction: drawn as its render mode says -- a puddle's
            // sheet -- but a volume the player stands in.)
            if (cls == "func_illusionary" || cls == "func_water" || cls == "func_friction")
                return true;
            if ((cls == "func_door" || cls == "func_door_rotating" || cls == "func_train" ||
                 cls == "func_tracktrain") &&
                (spawnflags & 8))
                return true;
            if (cls == "func_conveyor" && (spawnflags & 2))
                return true;
            if (cls == "func_rot_button" && (spawnflags & 1))
                return true;
            if (cls == "func_rotating" && (spawnflags & 64))
                return true;
            if (cls == "func_pendulum" && (spawnflags & 32))
                return true;
            return false;
        }

        /// Classes (and flags) the player USEs (the game's FCAP_IMPULSE_USE
        /// and FCAP_CONTINUOUS_USE).
        bool isUsable(const std::string &cls, int spawnflags)
        {
            if (cls == "func_button" || cls == "func_rot_button" || cls == "momentary_rot_button" ||
                cls == "func_healthcharger" || cls == "func_recharge")
                return true;
            return (cls == "func_door" || cls == "func_door_rotating") && (spawnflags & 256);
        }
    } // namespace

    std::vector<BrushInfo> analyseBrushes(const BspFile &bsp, const std::vector<Entity> &entities)
    {
        std::vector<BrushInfo> brushes;
        // The names an env_render shows (an amount above 0, and the amount
        // not left as it is: flag 2): c1a0d's burst stew, unseen until its
        // microwave goes off.
        std::vector<std::string> shown;
        for (const Entity &entity : entities)
        {
            if (entity.className() == "env_render" && !(intKey(entity, "spawnflags") & 2) &&
                intKey(entity, "renderamt") > 0 && !entity.value("target").empty())
                shown.emplace_back(entity.value("target"));
        }
        for (std::size_t e = 0; e < entities.size(); ++e)
        {
            const Entity &entity = entities[e];
            const std::string model(entity.value("model"));
            if (model.size() < 2 || model[0] != '*')
                continue;
            const int index = std::atoi(model.c_str() + 1);
            if (index <= 0 || index >= static_cast<int>(bsp.models.size()))
                continue;

            BrushInfo info;
            info.entity = static_cast<int>(e);
            info.model = index;
            info.className = std::string(entity.className());
            const int spawnflags = intKey(entity, "spawnflags");
            info.hasPivot = entity.vec3("origin", info.origin);

            const int mode = intKey(entity, "rendermode");
            const int amount = std::clamp(intKey(entity, "renderamt"), 0, 255);
            const int alpha = static_cast<int>(std::lround(amount * 31.0 / 255.0));

            if (isVolumeOnly(info.className))
            {
                info.renderClass = RenderClass::Hidden;
                info.solid = false;
                brushes.push_back(std::move(info));
                continue;
            }
            info.solid = !isPassable(info.className, spawnflags);
            info.usable = isUsable(info.className, spawnflags);
            info.startsOff = info.className == "func_wall_toggle" && (spawnflags & 1);

            switch (mode)
            {
            case 0:
                info.renderClass = RenderClass::Opaque;
                break;
            case 4:
                // Full strength whatever the amount says, with holes.
                info.renderClass = RenderClass::Cutout;
                break;
            case 1:
            {
                double rgb[3] = {255, 255, 255};
                entity.vec3("rendercolor", rgb);
                info.colour = hldata::rgb555(std::clamp(static_cast<int>(rgb[0]), 0, 255),
                                             std::clamp(static_cast<int>(rgb[1]), 0, 255),
                                             std::clamp(static_cast<int>(rgb[2]), 0, 255));
                info.renderClass = alpha == 0 ? RenderClass::Hidden : RenderClass::Colour;
                info.alpha = std::max(alpha, 1);
                break;
            }
            default: // 2 texture, 3 glow, 5 additive
                if (alpha == 0)
                    info.renderClass = RenderClass::Hidden;
                else if (alpha >= 31)
                    info.renderClass = RenderClass::Cutout; // nothing left to blend
                else
                {
                    info.renderClass = RenderClass::Blend;
                    info.alpha = alpha;
                }
                break;
            }
            // Shown later by an env_render: drawn as it will be shown (as made,
            // the amount being the env_render's business), and unseen until
            // then.
            if (info.renderClass == RenderClass::Hidden && !entity.value("targetname").empty() &&
                std::find(shown.begin(), shown.end(), std::string(entity.value("targetname"))) != shown.end())
            {
                info.renderClass = RenderClass::Opaque;
                info.alpha = 31;
                info.startsUnseen = true;
            }
            brushes.push_back(std::move(info));
        }
        return brushes;
    }

    std::vector<const BrushInfo *> brushOfModel(const BspFile &bsp,
                                                const std::vector<BrushInfo> &brushes)
    {
        std::vector<const BrushInfo *> byModel(bsp.models.size(), nullptr);
        for (const BrushInfo &brush : brushes)
        {
            // Two entities naming one model is legal and rare; the first is
            // the one whose looks the model is cooked with.
            if (byModel[brush.model] == nullptr)
                byModel[brush.model] = &brush;
        }
        return byModel;
    }
} // namespace hlds

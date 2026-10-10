#include "OpenLoco/Modding/Dependencies.hpp"

#include <algorithm>
#include <map>

namespace OpenLoco::Modding
{
    namespace
    {
        enum class Visit
        {
            none,
            active,
            done,
        };

        bool walk(
            const std::string& id,
            const std::map<std::string, std::vector<std::string>>& edges,
            std::map<std::string, Visit>& visits,
            std::vector<std::string>& stack,
            std::vector<std::string>& order,
            std::vector<std::string>& errors)
        {
            const auto state = visits[id];
            if (state == Visit::done)
            {
                return true;
            }
            if (state == Visit::active)
            {
                std::string cycle = "dependency cycle:";
                bool include = false;
                for (const auto& item : stack)
                {
                    if (item == id)
                    {
                        include = true;
                    }
                    if (include)
                    {
                        cycle += " " + item;
                    }
                }
                cycle += " " + id;
                errors.push_back(std::move(cycle));
                return false;
            }

            visits[id] = Visit::active;
            stack.push_back(id);
            bool ok = true;
            const auto found = edges.find(id);
            if (found != edges.end())
            {
                for (const auto& dependency : found->second)
                {
                    ok = walk(dependency, edges, visits, stack, order, errors) && ok;
                }
            }
            stack.pop_back();
            visits[id] = Visit::done;
            order.push_back(id);
            return ok;
        }
    }

    ResolveResult resolveDependencies(const std::vector<Manifest>& manifests)
    {
        ResolveResult result;
        std::map<std::string, const Manifest*> byId;
        for (const auto& manifest : manifests)
        {
            if (!byId.emplace(manifest.id, &manifest).second)
            {
                result.errors.push_back("duplicate mod id \"" + manifest.id + "\"");
            }
        }
        if (!result.errors.empty())
        {
            return result;
        }

        std::map<std::string, std::vector<std::string>> edges;
        std::vector<std::string> ids;
        ids.reserve(manifests.size());
        for (const auto& manifest : manifests)
        {
            ids.push_back(manifest.id);
            auto& dependencies = edges[manifest.id];
            for (const auto& dependency : manifest.dependencies)
            {
                if (byId.find(dependency) == byId.end())
                {
                    result.errors.push_back(manifest.id + " requires missing mod \"" + dependency + "\"");
                }
                else
                {
                    dependencies.push_back(dependency);
                }
            }
            for (const auto& dependency : manifest.optionalDependencies)
            {
                if (byId.find(dependency) != byId.end())
                {
                    dependencies.push_back(dependency);
                }
            }
            for (const auto& conflict : manifest.conflicts)
            {
                if (byId.find(conflict) != byId.end())
                {
                    result.errors.push_back(manifest.id + " conflicts with \"" + conflict + "\"");
                }
            }
            std::sort(dependencies.begin(), dependencies.end());
            dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        }

        if (!result.errors.empty())
        {
            return result;
        }

        std::sort(ids.begin(), ids.end());
        std::map<std::string, Visit> visits;
        std::vector<std::string> stack;
        for (const auto& id : ids)
        {
            walk(id, edges, visits, stack, result.loadOrder, result.errors);
        }
        if (!result.errors.empty())
        {
            result.loadOrder.clear();
        }
        return result;
    }
}

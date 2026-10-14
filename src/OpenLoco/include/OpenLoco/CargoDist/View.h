#pragma once
#include "CargoDist.h"
#include <OpenLoco/Ui/Window.h>
namespace OpenLoco::CargoDist
{
    struct DestinationSummary
    {
        uint8_t cargo;
        uint16_t destination, next;
        uint32_t quantity;
        bool operator==(const DestinationSummary&) const = default;
    };
    struct CargoSummary
    {
        std::vector<DestinationSummary> destinations;
        uint32_t unknown = 0;
        bool automatic = false, pending = false, limited = false;
    };
    CargoSummary summarize(const State& state, const std::vector<Holder>& holders, uint8_t company, uint16_t station, size_t& budget);
    struct CargoRow
    {
        uint16_t key;
        int32_t top, baseHeight, height;
        uint32_t quantity = 0;
        uint16_t origin = kUnknownStation;
        std::vector<std::string> information;
    };
    struct CargoRows
    {
        std::vector<CargoRow> rows;
        int32_t height = 0;
        const CargoRow* at(int32_t y) const;
    };
    CargoRows layoutCargoRows(const std::vector<CargoRow>& rows);
    const CargoRows& cargoRows(Ui::Window& window);
    void toggleCargoRow(Ui::Window& window, int16_t y);
    void closeCargoRows(Ui::Window& window);
}

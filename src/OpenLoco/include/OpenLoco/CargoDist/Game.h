#pragma once
#include "CargoDist.h"
#include "Economy/Currency.h"
#include "Types.hpp"
#include <OpenLoco/Engine/World.hpp>
namespace OpenLoco::Vehicles
{
    struct VehicleHead;
    struct VehicleCargo;
    struct VehicleBogie;
}
namespace OpenLoco
{
    struct Station;
}
namespace OpenLoco::CargoDist
{
    uint64_t componentGeneration(uint16_t component);
    uint64_t stationGeneration(uint16_t station);
    void syncStation(const OpenLoco::Station& station);
    void stationChanged(uint16_t station);
    void stationRemoved(uint16_t station);
    void componentRemoved(uint16_t component);
    void propertiesChanged(const Vehicles::VehicleHead& head);
    void movedCargo(uint16_t oldComponent, uint16_t newComponent, uint8_t slot);
    void vehicleChanged(const Vehicles::VehicleHead& head);
    void arrived(const Vehicles::VehicleHead& head);
    void ageVehicle(const Vehicles::VehicleHead& head);
    void ageStation(uint16_t station);
    uint32_t availableFor(const Vehicles::VehicleHead& head, uint8_t cargo);
    std::optional<uint32_t> load(Vehicles::VehicleHead& head, Vehicles::VehicleCargo& cargo, Vehicles::VehicleBogie& bogie);
    std::optional<bool> unload(Vehicles::VehicleHead& head, Vehicles::VehicleCargo& cargo, Vehicles::VehicleBogie& bogie, const std::function<void(const Packet&)>& nativeDelivery);
    World::Pos2 paymentOrigin(World::Pos2 fallback);
    currency32_t reportedPayment(currency32_t actual);
    currency32_t takeIncomeAdjustment(uint16_t head);
    uint32_t vehicleDestined(uint16_t head, uint8_t cargo, uint16_t destination);
    uint16_t lastStation(uint16_t vehicle);
    std::vector<Holder> vehicleHolders(uint16_t head);
    std::vector<Link> lineLinks(uint16_t line);
    std::string status();
    // Persistence consumes a validated native snapshot, with no UI mutations.
    std::map<Holder, std::pair<uint64_t, uint32_t>> nativeQuantities();
    State& gameState();
    void replaceState(State next);
}

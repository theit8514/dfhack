/*
https://github.com/peterix/dfhack
Copyright (c) 2009-2011 Petr Mrázek (peterix@gmail.com)

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/


#include "Internal.h"

#include <string>
#include <vector>
#include <map>
using namespace std;

#include "VersionInfo.h"
#include "MemAccess.h"
#include "Types.h"
#include "Error.h"
#include "modules/Buildings.h"
#include "modules/Maps.h"
#include "modules/Job.h"
#include "ModuleFactory.h"
#include "Core.h"
#include "TileTypes.h"
#include "MiscUtils.h"
using namespace DFHack;

#include "DataDefs.h"
#include "df/world.h"
#include "df/ui.h"
#include "df/d_init.h"
#include "df/item.h"
#include "df/job.h"
#include "df/job_item.h"
#include "df/general_ref_building_holderst.h"
#include "df/buildings_other_id.h"
#include "df/building_design.h"
#include "df/building_def.h"
#include "df/building_axle_horizontalst.h"
#include "df/building_trapst.h"
#include "df/building_bridgest.h"
#include "df/building_coffinst.h"
#include "df/building_furnacest.h"
#include "df/building_workshopst.h"
#include "df/building_screw_pumpst.h"
#include "df/building_water_wheelst.h"
#include "df/building_wellst.h"

using namespace df::enums;
using df::global::ui;
using df::global::world;
using df::global::d_init;
using df::global::building_next_id;
using df::global::process_jobs;
using df::building_def;

uint32_t Buildings::getNumBuildings()
{
    return world->buildings.all.size();
}

bool Buildings::Read (const uint32_t index, t_building & building)
{
    Core & c = Core::getInstance();
    df::building *bld = world->buildings.all[index];

    building.x1 = bld->x1;
    building.x2 = bld->x2;
    building.y1 = bld->y1;
    building.y2 = bld->y2;
    building.z = bld->z;
    building.material.index = bld->mat_index;
    building.material.type = bld->mat_type;
    building.type = bld->getType();
    building.subtype = bld->getSubtype();
    building.custom_type = bld->getCustomType();
    building.origin = bld;
    return true;
}

bool Buildings::ReadCustomWorkshopTypes(map <uint32_t, string> & btypes)
{
    vector <building_def *> & bld_def = world->raws.buildings.all;
    uint32_t size = bld_def.size();
    btypes.clear();

    for (auto iter = bld_def.begin(); iter != bld_def.end();iter++)
    {
        building_def * temp = *iter;
        btypes[temp->id] = temp->code;
    }
    return true;
}

df::building *Buildings::allocInstance(df::coord pos, df::building_type type, int subtype, int custom)
{
    if (!building_next_id)
        return NULL;

    // Allocate object
    const char *classname = ENUM_ATTR(building_type, classname, type);
    if (!classname)
        return NULL;

    auto id = virtual_identity::find(classname);
    if (!id)
        return NULL;

    df::building *bld = (df::building*)id->allocate();
    if (!bld)
        return NULL;

    // Init base fields
    bld->x1 = bld->x2 = bld->centerx = pos.x;
    bld->y1 = bld->y2 = bld->centery = pos.y;
    bld->z = pos.z;

    bld->race = ui->race_id;

    if (subtype != -1)
        bld->setSubtype(subtype);
    if (custom != -1)
        bld->setCustomType(custom);

    bld->setMaterialAmount(1);

    // Type specific init
    switch (type)
    {
    case building_type::Well:
        {
            auto obj = (df::building_wellst*)bld;
            obj->bucket_z = bld->z;
            break;
        }
    case building_type::Furnace:
        {
            auto obj = (df::building_furnacest*)bld;
            obj->melt_remainder.resize(df::inorganic_raw::get_vector().size(), 0);
            break;
        }
    case building_type::Coffin:
        {
            auto obj = (df::building_coffinst*)bld;
            obj->initBurialFlags(); // DF has this copy&pasted
            break;
        }
    case building_type::Trap:
        {
            auto obj = (df::building_trapst*)bld;
            if (obj->trap_type == trap_type::PressurePlate)
                obj->unk_cc = 500;
            break;
        }
    default:
        break;
    }

    return bld;
}

static void makeOneDim(df::coord2d &size, df::coord2d &center, bool vertical)
{
    if (vertical)
        size.x = 1;
    else
        size.y = 1;
    center = size/2;
}

bool Buildings::getCorrectSize(df::coord2d &size, df::coord2d &center,
                               df::building_type type, int subtype, int custom, int direction)
{
    using namespace df::enums::building_type;

    if (size.x <= 0)
        size.x = 1;
    if (size.y <= 0)
        size.y = 1;

    switch (type)
    {
    case FarmPlot:
    case Bridge:
    case RoadDirt:
    case RoadPaved:
    case Stockpile:
    case Civzone:
        center = size/2;
        return true;

    case TradeDepot:
    case Shop:
        size = df::coord2d(5,5);
        center = df::coord2d(2,2);
        return false;

    case SiegeEngine:
    case Windmill:
    case Wagon:
        size = df::coord2d(3,3);
        center = df::coord2d(1,1);
        return false;

    case AxleHorizontal:
        makeOneDim(size, center, direction);
        return true;

    case WaterWheel:
        size = df::coord2d(3,3);
        makeOneDim(size, center, direction);
        return false;

    case Workshop:
    {
        using namespace df::enums::workshop_type;

        switch ((df::workshop_type)subtype)
        {
            case Quern:
            case Millstone:
            case Tool:
                size = df::coord2d(1,1);
                center = df::coord2d(0,0);
                break;

            case Siege:
            case Kennels:
                size = df::coord2d(5,5);
                center = df::coord2d(2,2);
                break;

            case Custom:
                if (auto def = df::building_def::find(custom))
                {
                    size = df::coord2d(def->dim_x, def->dim_y);
                    center = df::coord2d(def->workloc_x, def->workloc_y);
                    break;
                }

            default:
                size = df::coord2d(3,3);
                center = df::coord2d(1,1);
        }

        return false;
    }

    case Furnace:
    {
        using namespace df::enums::furnace_type;

        switch ((df::furnace_type)subtype)
        {
            case Custom:
                if (auto def = df::building_def::find(custom))
                {
                    size = df::coord2d(def->dim_x, def->dim_y);
                    center = df::coord2d(def->workloc_x, def->workloc_y);
                    break;
                }

            default:
                size = df::coord2d(3,3);
                center = df::coord2d(1,1);
        }

        return false;
    }

    case ScrewPump:
    {
        using namespace df::enums::screw_pump_direction;

        switch ((df::screw_pump_direction)direction)
        {
            case FromEast:
                size = df::coord2d(2,1);
                center = df::coord2d(1,0);
                break;
            case FromSouth:
                size = df::coord2d(1,2);
                center = df::coord2d(0,1);
                break;
            case FromWest:
                size = df::coord2d(2,1);
                center = df::coord2d(0,0);
                break;
            default:
                size = df::coord2d(1,2);
                center = df::coord2d(0,0);
        }

        return false;
    }

    default:
        size = df::coord2d(1,1);
        center = df::coord2d(0,0);
        return false;
    }
}

static uint8_t *getExtentTile(df::building_extents &extent, df::coord2d tile)
{
    if (!extent.extents)
        return NULL;
    int dx = tile.x - extent.x;
    int dy = tile.y - extent.y;
    if (dx < 0 || dy < 0 || dx >= extent.width || dy >= extent.height)
        return NULL;
    return &extent.extents[dx + dy*extent.width];
}

bool Buildings::checkFreeTiles(df::coord pos, df::coord2d size,
                               df::building_extents *ext,
                               bool create_ext, bool allow_occupied)
{
    bool found_any = false;

    for (int dx = 0; dx < size.x; dx++)
    {
        for (int dy = 0; dy < size.y; dy++)
        {
            df::coord tile = pos + df::coord(dx,dy,0);
            uint8_t *etile = NULL;

            // Exclude using extents
            if (ext && ext->extents)
            {
                etile = getExtentTile(*ext, tile);
                if (!etile || !*etile)
                    continue;
            }

            // Look up map block
            df::map_block *block = Maps::getTileBlock(tile);
            if (!block)
                return false;

            df::coord2d btile = df::coord2d(tile) & 15;

            bool allowed = true;

            // Check occupancy and tile type
            if (!allow_occupied &&
                block->occupancy[btile.x][btile.y].bits.building)
                allowed = false;
            else
            {
                auto tile = block->tiletype[btile.x][btile.y];
                if (!HighPassable(tile))
                    allowed = false;
            }

            // Create extents if requested
            if (allowed)
                found_any = true;
            else
            {
                if (!ext || !create_ext)
                    return false;

                if (!ext->extents)
                {
                    ext->extents = new uint8_t[size.x*size.y];
                    ext->x = pos.x;
                    ext->y = pos.y;
                    ext->width = size.x;
                    ext->height = size.y;

                    memset(ext->extents, 1, size.x*size.y);
                    etile = getExtentTile(*ext, tile);
                }

                if (!etile)
                    return false;

                *etile = 0;
            }
        }
    }

    return found_any;
}

std::pair<df::coord,df::coord2d> Buildings::getSize(df::building *bld)
{
    CHECK_NULL_POINTER(bld);

    df::coord pos(bld->x1,bld->y1,bld->z);

    return std::pair<df::coord,df::coord2d>(pos, df::coord2d(bld->x2+1,bld->y2+1) - pos);
}

static bool checkBuildingTiles(df::building *bld, bool can_change)
{
    auto psize = Buildings::getSize(bld);

    return Buildings::checkFreeTiles(psize.first, psize.second, &bld->room,
                                     can_change && bld->isExtentShaped(),
                                     !bld->isSettingOccupancy());
}

int Buildings::countExtentTiles(df::building_extents *ext, int defval)
{
    if (!ext || !ext->extents)
        return defval;

    int cnt = 0;
    for (int i = 0; i < ext->width * ext->height; i++)
        if (ext->extents[i])
            cnt++;
    return cnt;
}

bool Buildings::hasSupport(df::coord pos, df::coord2d size)
{
    for (int dx = -1; dx <= size.x; dx++)
    {
        for (int dy = -1; dy <= size.y; dy++)
        {
            // skip corners
            if ((dx < 0 || dx == size.x) && (dy < 0 || dy == size.y))
                continue;

            df::coord tile = pos + df::coord(dx,dy,0);
            df::map_block *block = Maps::getTileBlock(tile);
            if (!block)
                continue;

            df::coord2d btile = df::coord2d(tile) & 15;
            if (!isOpenTerrain(block->tiletype[btile.x][btile.y]))
                return true;
        }
    }

    return false;
}

static int computeMaterialAmount(df::building *bld)
{
    auto size = Buildings::getSize(bld).second;
    int cnt = size.x * size.y;

    if (bld->room.extents && bld->isExtentShaped())
        cnt = Buildings::countExtentTiles(&bld->room, cnt);

    return cnt/4 + 1;
}

bool Buildings::setSize(df::building *bld, df::coord2d size, int direction)
{
    CHECK_NULL_POINTER(bld);
    CHECK_INVALID_ARGUMENT(bld->id == -1);

    // Delete old extents
    if (bld->room.extents)
    {
        delete[] bld->room.extents;
        bld->room.extents = NULL;
    }

    // Compute correct size and apply it
    df::coord2d center;
    getCorrectSize(size, center, bld->getType(), bld->getSubtype(),
                   bld->getCustomType(), direction);

    bld->x2 = bld->x1 + size.x - 1;
    bld->y2 = bld->y1 + size.y - 1;
    bld->centerx = bld->x1 + center.x;
    bld->centery = bld->y1 + center.y;

    auto type = bld->getType();

    using namespace df::enums::building_type;

    switch (type)
    {
    case WaterWheel:
        {
            auto obj = (df::building_water_wheelst*)bld;
            obj->is_vertical = !!direction;
            break;
        }
    case AxleHorizontal:
        {
            auto obj = (df::building_axle_horizontalst*)bld;
            obj->is_vertical = !!direction;
            break;
        }
    case ScrewPump:
        {
            auto obj = (df::building_screw_pumpst*)bld;
            obj->direction = (df::screw_pump_direction)direction;
            break;
        }
    case Bridge:
        {
            auto obj = (df::building_bridgest*)bld;
            auto psize = getSize(bld);
            obj->gate_flags.bits.has_support = hasSupport(psize.first, psize.second);
            obj->direction = (df::building_bridgest::T_direction)direction;
            break;
        }
    default:
        break;
    }

    bool ok = checkBuildingTiles(bld, true);

    if (type != building_type::Construction)
        bld->setMaterialAmount(computeMaterialAmount(bld));

    return ok;
}

static void markBuildingTiles(df::building *bld, bool remove)
{
    bool use_extents = bld->room.extents && bld->isExtentShaped();
    bool stockpile = (bld->getType() == building_type::Stockpile);
    bool complete = (bld->getBuildStage() >= bld->getMaxBuildStage());

    if (remove)
        stockpile = complete = false;

    for (int tx = bld->x1; tx <= bld->x2; tx++)
    {
        for (int ty = bld->y1; ty <= bld->y2; ty++)
        {
            df::coord tile(tx,ty,bld->z);

            if (use_extents)
            {
                uint8_t *etile = getExtentTile(bld->room, tile);
                if (!etile || !*etile)
                    continue;
            }

            df::map_block *block = Maps::getTileBlock(tile);
            df::coord2d btile = df::coord2d(tile) & 15;

            auto &des = block->designation[btile.x][btile.y];

            des.bits.pile = stockpile;
            if (!remove)
                des.bits.dig = tile_dig_designation::No;

            if (complete)
                bld->updateOccupancy(tx, ty);
            else
            {
                auto &occ = block->occupancy[btile.x][btile.y];

                if (remove)
                    occ.bits.building = tile_building_occ::None;
                else
                    occ.bits.building = tile_building_occ::Planned;
            }
        }
    }
}

static void linkRooms(df::building *bld)
{
    auto &vec = world->buildings.other[buildings_other_id::ANY_FREE];

    bool changed = false;

    for (size_t i = 0; i < vec.size(); i++)
    {
        auto room = vec[i];
        if (!room->is_room || room->z != bld->z)
            continue;

        uint8_t *pext = getExtentTile(room->room, df::coord2d(bld->x1, bld->y1));
        if (!pext || !*pext)
            continue;

        changed = true;
        room->children.push_back(bld);
        bld->parents.push_back(room);

        // TODO: the game updates room rent here if economy is enabled
    }

    if (changed)
        df::global::ui->equipment.update.bits.buildings = true;
}

static void linkBuilding(df::building *bld)
{
    bld->id = (*building_next_id)++;

    world->buildings.all.push_back(bld);
    bld->categorize(true);

    if (bld->isSettingOccupancy())
        markBuildingTiles(bld, false);

    linkRooms(bld);

    if (process_jobs)
        *process_jobs = true;
}

static void createDesign(df::building *bld, bool rough)
{
    auto job = bld->jobs[0];

    job->mat_type = bld->mat_type;
    job->mat_index = bld->mat_index;

    if (bld->needsDesign())
    {
        auto act = (df::building_actual*)bld;
        act->design = new df::building_design();

        act->design->flags.bits.rough = rough;
    }
}

static bool linkForConstruct(df::job* &job, df::building *bld)
{
    if (!checkBuildingTiles(bld, false))
        return false;

    auto ref = df::allocate<df::general_ref_building_holderst>();
    if (!ref)
    {
        Core::printerr("Could not allocate general_ref_building_holderst\n");
        return false;
    }

    linkBuilding(bld);

    ref->building_id = bld->id;

    job = new df::job();
    job->job_type = df::job_type::ConstructBuilding;
    job->pos = df::coord(bld->centerx, bld->centery, bld->z);
    job->references.push_back(ref);

    bld->jobs.push_back(job);

    Job::linkIntoWorld(job);

    return true;
}

bool Buildings::constructWithItems(df::building *bld, std::vector<df::item*> items)
{
    CHECK_NULL_POINTER(bld);
    CHECK_INVALID_ARGUMENT(!items.empty());
    CHECK_INVALID_ARGUMENT(bld->id == -1);
    CHECK_INVALID_ARGUMENT(bld->isActual());

    for (size_t i = 0; i < items.size(); i++)
    {
        CHECK_NULL_POINTER(items[i]);

        if (items[i]->flags.bits.in_job)
            return false;
    }

    df::job *job = NULL;
    if (!linkForConstruct(job, bld))
        return false;

    bool rough = false;

    for (size_t i = 0; i < items.size(); i++)
    {
        Job::attachJobItem(job, items[i], df::job_item_ref::Hauled);

        if (items[i]->getType() == item_type::BOULDER)
            rough = true;
        if (bld->mat_type == -1)
            bld->mat_type = items[i]->getMaterial();
        if (bld->mat_index == -1)
            bld->mat_index = items[i]->getMaterialIndex();
    }

    createDesign(bld, rough);
    return true;
}

bool Buildings::constructWithFilters(df::building *bld, std::vector<df::job_item*> items)
{
    CHECK_NULL_POINTER(bld);
    CHECK_INVALID_ARGUMENT(!items.empty());
    CHECK_INVALID_ARGUMENT(bld->id == -1);
    CHECK_INVALID_ARGUMENT(bld->isActual());

    for (size_t i = 0; i < items.size(); i++)
        CHECK_NULL_POINTER(items[i]);

    df::job *job = NULL;
    if (!linkForConstruct(job, bld))
    {
        for (size_t i = 0; i < items.size(); i++)
            delete items[i];

        return false;
    }

    bool rough = false;

    for (size_t i = 0; i < items.size(); i++)
    {
        if (items[i]->quantity < 0)
            items[i]->quantity = computeMaterialAmount(bld);

        job->job_items.push_back(items[i]);

        if (items[i]->item_type == item_type::BOULDER)
            rough = true;
        if (bld->mat_type == -1)
            bld->mat_type = items[i]->mat_type;
        if (bld->mat_index == -1)
            bld->mat_index = items[i]->mat_index;
    }

    createDesign(bld, rough);
    return true;
}


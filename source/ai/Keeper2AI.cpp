/*!
 *  Copyright (C) 2011-2014  OpenDungeons Team
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ai/Keeper2AI.h"

#include "entities/Creature.h"
#include "gamemap/GameMap.h"
#include "entities/Tile.h"
#include "rooms/RoomCrypt.h"
#include "rooms/RoomDormitory.h"
#include "rooms/RoomForge.h"
#include "rooms/RoomHatchery.h"
#include "rooms/RoomLibrary.h"
#include "rooms/RoomTrainingHall.h"
#include "rooms/RoomTreasury.h"
#include "utils/LogManager.h"
#include "utils/Random.h"

#include <vector>

AIFactoryRegister<Keeper2AI> Keeper2AI::reg("Keeper2AI");

const double TIME_LOOK_ROOM = 60.0 * 2.0;
const double TIME_LOOK_GOLD = 60.0 * 10.0;

Keeper2AI::Keeper2AI(GameMap& gameMap, Player& player, const std::string& parameters):
    BaseAI(gameMap, player, parameters),
    mLastTimeLookingForRooms(0.0),
    mRoomPosX(-1),
    mRoomPosY(-1),
    mRoomSize(-1),
    mNoMoreReachableGold(false),
    mLastTimeLookingForGold(0.0),
    mCooldownDefense(0)
{
}

bool Keeper2AI::doTurn(double frameTime)
{
    // If we have no dungeon temple, we are dead
    if(mAiWrapper.getDungeonTemple() == nullptr)
        return false;

    saveWoundedCreatures();

    handleDefense();

    if (checkTreasury())
        return true;

    if (handleRooms(frameTime))
        return true;

    if (lookForGold(frameTime))
        return true;

    return true;
}

bool Keeper2AI::checkTreasury()
{
    GameMap& gameMap = mAiWrapper.getGameMap();
    Player& player = mAiWrapper.getPlayer();
    std::vector<Room*> treasuriesOwned = gameMap.getRoomsByTypeAndSeat(Room::treasury,
        player.getSeat());

    int nbTilesTreasuries = 0;
    for(Room* treasury : treasuriesOwned)
        nbTilesTreasuries += treasury->getCoveredTiles().size();

    // We want at least 3 tiles for a treasury
    if(nbTilesTreasuries >= 3)
        return false;

    // The treasury is too small, we try to increase it
    int totalGold = 0;
    for(Room* treasury : treasuriesOwned)
    {
        RoomTreasury* rt = static_cast<RoomTreasury*>(treasury);
        totalGold += rt->getTotalGold();
    }

    // A treasury can be built if we have none (in this case, it is free). Otherwise,
    // we check if we have enough gold
    if(nbTilesTreasuries > 0 && totalGold < Room::costPerTile(Room::RoomType::treasury))
        return false;

    Tile* central = mAiWrapper.getDungeonTemple()->getCentralTile();

    Creature* kobold = nullptr;
    for (Creature* creature : gameMap.getCreaturesBySeat(player.getSeat()))
    {
        if (creature->getDefinition()->getClassName() == "Kobold")
        {
            kobold = creature;
            break;
        }
    }

    if (kobold == nullptr)
        return false;

    // We try in priority to gold next to an existing treasury
    for(Room* treasury : treasuriesOwned)
    {
        for(Tile* tile : treasury->getCoveredTiles())
        {
            for(Tile* neigh : tile->getAllNeighbors())
            {
                if(neigh->isBuildableUpon() && neigh->isClaimedForSeat(player.getSeat()) &&
                   gameMap.pathExists(kobold, central, neigh))
                {
                    std::vector<Tile*> tiles;
                    int goldRequired;
                    gameMap.fillBuildableTilesAndPriceForPlayerInArea(neigh->getX(), neigh->getY(),
                        neigh->getX(), neigh->getY(), &player, Room::RoomType::treasury, tiles, goldRequired);
                    if (tiles.empty())
                        return false;

                    if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
                        return false;

                    Room* room = new RoomTreasury(&gameMap);
                    mAiWrapper.buildRoom(room, tiles);
                    return true;
                }
            }
        }
    }

    int widerSide = gameMap.getMapSizeX() > gameMap.getMapSizeY() ?
        gameMap.getMapSizeX() : gameMap.getMapSizeY();

    // We search for the closest gold tile
    Tile* firstAvailableTile = nullptr;
    for(int32_t distance = 1; distance < widerSide; ++distance)
    {
        for(int k = 0; k <= distance; ++k)
        {
            Tile* t;
            // North-East
            t = gameMap.getTile(central->getX() + k, central->getY() + distance);
            if(t != nullptr && t->isBuildableUpon() && t->isClaimedForSeat(player.getSeat()) &&
               gameMap.pathExists(kobold, central, t))
            {
                firstAvailableTile = t;
                break;
            }
            // North-West
            if(k > 0)
            {
                t = gameMap.getTile(central->getX() - k, central->getY() + distance);
                if(t != nullptr && t->isBuildableUpon() && t->isClaimedForSeat(player.getSeat()) &&
                   gameMap.pathExists(kobold, central, t))
                {
                    firstAvailableTile = t;
                    break;
                }
            }
            // South-East
            t = gameMap.getTile(central->getX() + k, central->getY() - distance);
            if(t != nullptr && t->isBuildableUpon() && t->isClaimedForSeat(player.getSeat()) &&
               gameMap.pathExists(kobold, central, t))
            {
                firstAvailableTile = t;
                break;
            }
            // South-West
            if(k > 0)
            {
                t = gameMap.getTile(central->getX() - k, central->getY() - distance);
                if(t != nullptr && t->isBuildableUpon() && t->isClaimedForSeat(player.getSeat()) &&
                   gameMap.pathExists(kobold, central, t))
                {
                    firstAvailableTile = t;
                    break;
                }
            }
            // East-North
            t = gameMap.getTile(central->getX() + distance, central->getY() + k);
            if(t != nullptr && t->isBuildableUpon() && t->isClaimedForSeat(player.getSeat()) &&
               gameMap.pathExists(kobold, central, t))
            {
                firstAvailableTile = t;
                break;
            }
            // East-South
            if(k > 0)
            {
                t = gameMap.getTile(central->getX() + distance, central->getY() - k);
                if(t != nullptr && t->isBuildableUpon() && t->isClaimedForSeat(player.getSeat()) &&
                   gameMap.pathExists(kobold, central, t))
                {
                    firstAvailableTile = t;
                    break;
                }
            }
            // West-North
            t = gameMap.getTile(central->getX() - distance, central->getY() + k);
            if(t != nullptr && t->isBuildableUpon() && t->isClaimedForSeat(player.getSeat()) &&
               gameMap.pathExists(kobold, central, t))
            {
                firstAvailableTile = t;
                break;
            }
            // West-South
            if(k > 0)
            {
                t = gameMap.getTile(central->getX() - distance, central->getY() - k);
                if(t != nullptr && t->isBuildableUpon() && t->isClaimedForSeat(player.getSeat()) &&
                   gameMap.pathExists(kobold, central, t))
                {
                    firstAvailableTile = t;
                    break;
                }
            }
        }

        if(firstAvailableTile != nullptr)
            break;
    }

    // We couldn't find any available tile T_T
    // We return true to avoid doing something else to let kobolds claim
    if(firstAvailableTile == nullptr)
        return true;

    std::vector<Tile*> tiles;
    int goldRequired;
    gameMap.fillBuildableTilesAndPriceForPlayerInArea(firstAvailableTile->getX(), firstAvailableTile->getY(),
        firstAvailableTile->getX(), firstAvailableTile->getY(), &player, Room::RoomType::treasury, tiles, goldRequired);
    if (tiles.empty())
        return false;

    if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
        return false;

    Room* room = new RoomTreasury(&gameMap);
    mAiWrapper.buildRoom(room, tiles);
    return true;
}

bool Keeper2AI::handleRooms(double frameTime)
{
    if(mLastTimeLookingForRooms > 0.0)
    {
        mLastTimeLookingForRooms -= frameTime * 10.0;
        return false;
    }

    mLastTimeLookingForRooms = TIME_LOOK_ROOM;

    // We check if the last built room is done
    if(mRoomSize != -1)
    {
        Tile* tile = mAiWrapper.getGameMap().getTile(mRoomPosX, mRoomPosY);
        OD_ASSERT_TRUE(tile != nullptr);
        if(tile == nullptr)
        {
            mRoomSize = -1;
            return false;
        }
        int32_t points;
        if(!mAiWrapper.computePointsForRoom(tile, mAiWrapper.getPlayer().getSeat(), mRoomSize, true, false, points))
        {
            // The room is not valid anymore (may be claimed or built by somebody else). We redo
            mRoomSize = -1;
            return false;
        }

        if(buildMostNeededRoom())
        {
            mRoomSize = -1;
            return true;
        }

        return false;
    }

    Tile* central = mAiWrapper.getDungeonTemple()->getCentralTile();
    int32_t bestX = 0;
    int32_t bestY = 0;
    if(!mAiWrapper.findBestPlaceForRoom(central, mAiWrapper.getPlayer().getSeat(), 5, true, bestX, bestY))
        return false;

    mRoomSize = 5;
    mRoomPosX = bestX;
    mRoomPosY = bestY;

    Tile* tileDest = mAiWrapper.getGameMap().getTile(mRoomPosX, mRoomPosY);
    OD_ASSERT_TRUE_MSG(tileDest != nullptr, "tileDest=" + Tile::displayAsString(tileDest));
    if(!mAiWrapper.digWayToTile(central, tileDest))
        return false;

    for(int xx = 0; xx < mRoomSize; ++xx)
    {
        for(int yy = 0; yy < mRoomSize; ++yy)
        {
            Tile* tile = mAiWrapper.getGameMap().getTile(mRoomPosX + xx, mRoomPosY + yy);
            OD_ASSERT_TRUE(tile != nullptr);
            if(tile == nullptr)
                continue;

            mAiWrapper.markTileForDigging(tile);
        }
    }

    return true;
}

bool Keeper2AI::lookForGold(double frameTime)
{
    if (mNoMoreReachableGold)
        return false;

    if(mLastTimeLookingForGold > 0.0)
    {
        mLastTimeLookingForGold -= frameTime * 10.0;
        return false;
    }

    mLastTimeLookingForGold = TIME_LOOK_GOLD;

    GameMap& gameMap = mAiWrapper.getGameMap();
    // Do we need gold ?
    int emptyStorage = 0;
    std::vector<Room*> treasuriesOwned = gameMap.getRoomsByTypeAndSeat(Room::treasury,
        mAiWrapper.getPlayer().getSeat());
    for(Room* room : treasuriesOwned)
    {
        RoomTreasury* rt = static_cast<RoomTreasury*>(room);
        emptyStorage += rt->emptyStorageSpace();
    }

    // No need to search for gold
    if(emptyStorage < 100)
        return false;

    Tile* central = mAiWrapper.getDungeonTemple()->getCentralTile();
    int widerSide = gameMap.getMapSizeX() > gameMap.getMapSizeY() ?
        gameMap.getMapSizeX() : gameMap.getMapSizeY();

    // We search for the closest gold tile
    Tile* firstGoldTile = nullptr;
    for(int32_t distance = 1; distance < widerSide; ++distance)
    {
        for(int k = 0; k <= distance; ++k)
        {
            Tile* t;
            // North-East
            t = gameMap.getTile(central->getX() + k, central->getY() + distance);
            if(t != nullptr && t->getType() == Tile::gold && t->getFullness() > 0.0)
            {
                // If we already have a tile at same distance, we randomly change to
                // try to not be too predictable
                if((firstGoldTile == nullptr) || (Random::Uint(1,2) == 1))
                    firstGoldTile = t;
            }
            // North-West
            if(k > 0)
            {
                t = gameMap.getTile(central->getX() - k, central->getY() + distance);
                if(t != nullptr && t->getType() == Tile::gold && t->getFullness() > 0.0)
                {
                    if((firstGoldTile == nullptr) || (Random::Uint(1,2) == 1))
                        firstGoldTile = t;
                }
            }
            // South-East
            t = gameMap.getTile(central->getX() + k, central->getY() - distance);
            if(t != nullptr && t->getType() == Tile::gold && t->getFullness() > 0.0)
            {
                if((firstGoldTile == nullptr) || (Random::Uint(1,2) == 1))
                    firstGoldTile = t;
            }
            // South-West
            if(k > 0)
            {
                t = gameMap.getTile(central->getX() - k, central->getY() - distance);
                if(t != nullptr && t->getType() == Tile::gold && t->getFullness() > 0.0)
                {
                    if((firstGoldTile == nullptr) || (Random::Uint(1,2) == 1))
                        firstGoldTile = t;
                }
            }
            // East-North
            t = gameMap.getTile(central->getX() + distance, central->getY() + k);
            if(t != nullptr && t->getType() == Tile::gold && t->getFullness() > 0.0)
            {
                if((firstGoldTile == nullptr) || (Random::Uint(1,2) == 1))
                    firstGoldTile = t;
            }
            // East-South
            if(k > 0)
            {
                t = gameMap.getTile(central->getX() + distance, central->getY() - k);
                if(t != nullptr && t->getType() == Tile::gold && t->getFullness() > 0.0)
                {
                    if((firstGoldTile == nullptr) || (Random::Uint(1,2) == 1))
                        firstGoldTile = t;
                }
            }
            // West-North
            t = gameMap.getTile(central->getX() - distance, central->getY() + k);
            if(t != nullptr && t->getType() == Tile::gold && t->getFullness() > 0.0)
            {
                if((firstGoldTile == nullptr) || (Random::Uint(1,2) == 1))
                    firstGoldTile = t;
            }
            // West-South
            if(k > 0)
            {
                t = gameMap.getTile(central->getX() - distance, central->getY() - k);
                if(t != nullptr && t->getType() == Tile::gold && t->getFullness() > 0.0)
                {
                    if((firstGoldTile == nullptr) || (Random::Uint(1,2) == 1))
                        firstGoldTile = t;
                }
            }

            if(firstGoldTile != nullptr)
                break;
        }

        // If we found a tile, no need to continue
        if(firstGoldTile != nullptr)
            break;
    }

    // No more gold
    if (firstGoldTile == nullptr)
    {
        mNoMoreReachableGold = true;
        return false;
    }

    if(!mAiWrapper.digWayToTile(central, firstGoldTile))
    {
        mNoMoreReachableGold = true;
        return false;
    }

    // If the neighbors are gold, we dig them
    const int levelTilesDig = 2;
    std::set<Tile*> tilesDig;
    // Because we can't insert Tiles in tilesDig while iterating, we use a copy: tilesDigTmp
    std::set<Tile*> tilesDigTmp;
    tilesDig.insert(firstGoldTile);

    for(int i = 0; i < levelTilesDig; ++i)
    {
        tilesDigTmp = tilesDig;
        for(Tile* tile : tilesDigTmp)
        {
            for(Tile* neigh : tile->getAllNeighbors())
            {
                if(neigh->getType() == Tile::gold && neigh->getFullness() > 0.0)
                    tilesDig.insert(neigh);
            }
        }
    }

    for(Tile* tile : tilesDig)
        mAiWrapper.markTileForDigging(tile);

    return true;
}

bool Keeper2AI::buildMostNeededRoom()
{
    GameMap& gameMap = mAiWrapper.getGameMap();
    Player& player = mAiWrapper.getPlayer();
    std::vector<Room*> rooms;

    // Dormitory
    rooms = gameMap.getRoomsByTypeAndSeat(Room::RoomType::dormitory, player.getSeat());
    uint32_t nbDormitory = rooms.size();
    if(nbDormitory == 0)
    {
        std::vector<Tile*> tiles;
        int goldRequired;
        gameMap.fillBuildableTilesAndPriceForPlayerInArea(mRoomPosX, mRoomPosY, mRoomPosX + mRoomSize - 1,
            mRoomPosY + mRoomSize - 1, &player, Room::RoomType::dormitory, tiles, goldRequired);
        if (tiles.size() < static_cast<uint32_t>(mRoomSize * mRoomSize))
            return false;

        if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
            return false;

        Room* room = new RoomDormitory(&gameMap);
        mAiWrapper.buildRoom(room, tiles);
        return true;
    }

    std::vector<Room*> treasuriesOwned = gameMap.getRoomsByTypeAndSeat(Room::treasury,
        player.getSeat());
    int emptyStorage = 0;
    int totalGold = 0;
    for(Room* room : treasuriesOwned)
    {
        RoomTreasury* rt = static_cast<RoomTreasury*>(room);
        emptyStorage += rt->emptyStorageSpace();
        totalGold += rt->getTotalGold();
    }

    if((emptyStorage < 100) && (totalGold < 20000))
    {
        std::vector<Tile*> tiles;
        int goldRequired;
        gameMap.fillBuildableTilesAndPriceForPlayerInArea(mRoomPosX, mRoomPosY, mRoomPosX + mRoomSize - 1,
            mRoomPosY + mRoomSize - 1, &player, Room::RoomType::treasury, tiles, goldRequired);
        if (tiles.size() < static_cast<uint32_t>(mRoomSize * mRoomSize))
            return false;

        if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
            return false;

        Room* room = new RoomTreasury(&gameMap);
        mAiWrapper.buildRoom(room, tiles);
        return true;
    }

    // hatchery
    rooms = gameMap.getRoomsByTypeAndSeat(Room::RoomType::hatchery, player.getSeat());
    if(rooms.empty())
    {
        std::vector<Tile*> tiles;
        int goldRequired;
        gameMap.fillBuildableTilesAndPriceForPlayerInArea(mRoomPosX, mRoomPosY, mRoomPosX + mRoomSize - 1,
            mRoomPosY + mRoomSize - 1, &player, Room::RoomType::hatchery, tiles, goldRequired);
        if (tiles.size() < static_cast<uint32_t>(mRoomSize * mRoomSize))
            return false;

        if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
            return false;

        Room* room = new RoomHatchery(&gameMap);
        mAiWrapper.buildRoom(room, tiles);
        return true;
    }

    // trainingHall
    rooms = gameMap.getRoomsByTypeAndSeat(Room::RoomType::trainingHall, player.getSeat());
    if(rooms.empty())
    {
        std::vector<Tile*> tiles;
        int goldRequired;
        gameMap.fillBuildableTilesAndPriceForPlayerInArea(mRoomPosX, mRoomPosY, mRoomPosX + mRoomSize - 1,
            mRoomPosY + mRoomSize - 1, &player, Room::RoomType::trainingHall, tiles, goldRequired);
        if (tiles.size() < static_cast<uint32_t>(mRoomSize * mRoomSize))
            return false;

        if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
            return false;

        Room* room = new RoomTrainingHall(&gameMap);
        mAiWrapper.buildRoom(room, tiles);
        return true;
    }

    // forge
    rooms = gameMap.getRoomsByTypeAndSeat(Room::RoomType::forge, player.getSeat());
    if(rooms.empty())
    {
        std::vector<Tile*> tiles;
        int goldRequired;
        gameMap.fillBuildableTilesAndPriceForPlayerInArea(mRoomPosX, mRoomPosY, mRoomPosX + mRoomSize - 1,
            mRoomPosY + mRoomSize - 1, &player, Room::RoomType::forge, tiles, goldRequired);
        if (tiles.size() < static_cast<uint32_t>(mRoomSize * mRoomSize))
            return false;

        if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
            return false;

        Room* room = new RoomForge(&gameMap);
        mAiWrapper.buildRoom(room, tiles);
        return true;
    }

    // library
    rooms = gameMap.getRoomsByTypeAndSeat(Room::RoomType::library, player.getSeat());
    if(rooms.empty())
    {
        std::vector<Tile*> tiles;
        int goldRequired;
        gameMap.fillBuildableTilesAndPriceForPlayerInArea(mRoomPosX, mRoomPosY, mRoomPosX + mRoomSize - 1,
            mRoomPosY + mRoomSize - 1, &player, Room::RoomType::library, tiles, goldRequired);
        if (tiles.size() < static_cast<uint32_t>(mRoomSize * mRoomSize))
            return false;

        if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
            return false;

        Room* room = new RoomLibrary(&gameMap);
        mAiWrapper.buildRoom(room, tiles);
        return true;
    }

    // Once we have done all the basic buildings, we go for another dormitory
    if(nbDormitory == 1)
    {
        std::vector<Tile*> tiles;
        int goldRequired;
        gameMap.fillBuildableTilesAndPriceForPlayerInArea(mRoomPosX, mRoomPosY, mRoomPosX + mRoomSize - 1,
            mRoomPosY + mRoomSize - 1, &player, Room::RoomType::dormitory, tiles, goldRequired);
        if (tiles.size() < static_cast<uint32_t>(mRoomSize * mRoomSize))
            return false;

        if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
            return false;

        Room* room = new RoomDormitory(&gameMap);
        mAiWrapper.buildRoom(room, tiles);
        return true;
    }

    // Crypt
    rooms = gameMap.getRoomsByTypeAndSeat(Room::RoomType::crypt, player.getSeat());
    if(rooms.empty())
    {
        std::vector<Tile*> tiles;
        int goldRequired;
        gameMap.fillBuildableTilesAndPriceForPlayerInArea(mRoomPosX, mRoomPosY, mRoomPosX + mRoomSize - 1,
            mRoomPosY + mRoomSize - 1, &player, Room::RoomType::crypt, tiles, goldRequired);
        if (tiles.size() < static_cast<uint32_t>(mRoomSize * mRoomSize))
            return false;

        if(!gameMap.withdrawFromTreasuries(goldRequired, player.getSeat()))
            return false;

        Room* room = new RoomCrypt(&gameMap);
        mAiWrapper.buildRoom(room, tiles);
        return true;
    }

    return true;
}

void Keeper2AI::saveWoundedCreatures()
{
    Tile* dungeonTempleTile = mAiWrapper.getDungeonTemple()->getCentralTile();
    OD_ASSERT_TRUE(dungeonTempleTile != nullptr);

    Seat* seat = mAiWrapper.getPlayer().getSeat();
    std::vector<Creature*> creatures = mAiWrapper.getGameMap().getCreaturesBySeat(seat);
    for(Creature* creature : creatures)
    {
        // We take away fleeing creatures not too near our dungeon heart
        if(!creature->isActionInList(CreatureAction::ActionType::flee))
            continue;
        Tile* tile = creature->getPositionTile();
        if(tile == nullptr)
            continue;

        if((std::abs(dungeonTempleTile->getX() - tile->getX()) <= 5) &&
           (std::abs(dungeonTempleTile->getY() - tile->getY()) <= 5))
        {
            // We are too close from our dungeon heart to be picked up
            continue;
        }

        if(!creature->tryPickup(seat, false))
            continue;

        mAiWrapper.getPlayer().pickUpEntity(creature, false);

        OD_ASSERT_TRUE(mAiWrapper.getPlayer().dropHand(dungeonTempleTile) == creature);
    }
}

void Keeper2AI::handleDefense()
{
    if(mCooldownDefense > 0)
    {
        --mCooldownDefense;
        return;
    }

    Tile* dungeonTempleTile = mAiWrapper.getDungeonTemple()->getCentralTile();
    OD_ASSERT_TRUE(dungeonTempleTile != nullptr);

    GameMap& gameMap = mAiWrapper.getGameMap();
    Seat* seat = mAiWrapper.getPlayer().getSeat();
    // We drop creatures nearby owned or allied attacked creatures
    std::vector<Creature*> creatures = gameMap.getCreaturesByAlliedSeat(seat);
    for(Creature* creature : creatures)
    {
        // We check if a creature is fighting near a claimed tile. If yes, we drop a creature nearby
        if(!creature->isActionInList(CreatureAction::ActionType::fight))
            continue;

        Tile* tile = creature->getPositionTile();
        if(tile == nullptr)
            continue;

        Creature* creatureToDrop = gameMap.getFighterToPickupBySeat(seat);
        if(creatureToDrop == nullptr)
            continue;

        if(!creatureToDrop->tryPickup(seat, false))
            continue;

        for(Tile* neigh : tile->getAllNeighbors())
        {
            if(creatureToDrop->tryDrop(seat, neigh, false))
            {
                mAiWrapper.getPlayer().pickUpEntity(creatureToDrop, false);
                OD_ASSERT_TRUE(mAiWrapper.getPlayer().dropHand(neigh) == creatureToDrop);
                mCooldownDefense = Random::Int(0,5);
                return;
            }
        }
    }
}
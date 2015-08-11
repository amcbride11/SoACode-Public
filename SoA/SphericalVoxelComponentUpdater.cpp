#include "stdafx.h"
#include "SphericalVoxelComponentUpdater.h"

#include <SDL/SDL_timer.h> // For SDL_GetTicks

#include "Chunk.h"
#include "ChunkAllocator.h"
#include "ChunkGrid.h"
#include "ChunkIOManager.h"
#include "ChunkMeshManager.h"
#include "ChunkMeshTask.h"
#include "ChunkRenderer.h"
#include "ChunkUpdater.h"
#include "FloraTask.h"
#include "GameSystem.h"
#include "GenerateTask.h"
#include "ParticleEngine.h"
#include "PhysicsEngine.h"
#include "PlanetGenData.h"
#include "SoaOptions.h"
#include "SoaState.h"
#include "SpaceSystem.h"
#include "SpaceSystemComponents.h"
#include "VoxelSpaceConversions.h"
#include "soaUtils.h"

#include <Vorb/voxel/VoxCommon.h>

void SphericalVoxelComponentUpdater::update(const SoaState* soaState) {
    SpaceSystem* spaceSystem = soaState->spaceSystem;
    GameSystem* gameSystem = soaState->gameSystem;
    if (spaceSystem->sphericalVoxel.getComponentListSize() > 1) {
        for (auto& it : spaceSystem->sphericalVoxel) {
            if (it.second.chunkGrids) {
                updateComponent(it.second);
            }
        }
    }
}

void SphericalVoxelComponentUpdater::updateComponent(SphericalVoxelComponent& cmp) {
    m_cmp = &cmp;
    // Update each world cube face
    for (int i = 0; i < 6; i++) {
        updateChunks(cmp.chunkGrids[i], true);
        cmp.chunkGrids[i].update();
    }
}

void SphericalVoxelComponentUpdater::updateChunks(ChunkGrid& grid, bool doGen) {
    // Get render distance squared
    f32 renderDist2 = (soaOptions.get(OPT_VOXEL_RENDER_DISTANCE).value.f + (f32)CHUNK_WIDTH);
    renderDist2 *= renderDist2;

}

void SphericalVoxelComponentUpdater::requestChunkMesh(ChunkHandle chunk) {
    // If it has no solid blocks, don't mesh it
    if (chunk->numBlocks <= 0) {
        chunk->remeshFlags = 0;
        return;
    }

    if (/*chunk->inFrustum && */!chunk->queuedForMesh) {
        ChunkMeshTask* meshTask = trySetMeshDependencies(chunk);

        if (meshTask) {
            m_cmp->threadPool->addTask(meshTask);
            chunk->remeshFlags = 0;
        }
    }
}

void SphericalVoxelComponentUpdater::disposeChunkMesh(Chunk* chunk) {
    // delete the mesh!
    if (chunk->hasCreatedMesh) {
        ChunkMeshMessage msg;
        msg.chunkID = chunk->getID();
        msg.messageID = ChunkMeshMessageID::DESTROY;
       // m_cmp->chunkMeshManager->sendMessage(msg);
        chunk->hasCreatedMesh = false;
        chunk->remeshFlags |= 1;
    }
}

ChunkMeshTask* SphericalVoxelComponentUpdater::trySetMeshDependencies(ChunkHandle chunk) {

    // TODO(Ben): This could be optimized a bit
    ChunkHandle& left = chunk->left;
    ChunkHandle& right = chunk->right;
    ChunkHandle& bottom = chunk->bottom;
    ChunkHandle& top = chunk->top;
    ChunkHandle& back = chunk->back;
    ChunkHandle& front = chunk->front;

    //// Check that neighbors are loaded
    if (!left->isAccessible || !right->isAccessible ||
        !front->isAccessible || !back->isAccessible ||
        !top->isAccessible || !bottom->isAccessible) return nullptr;
    
    // Left half
    if (!left->back.isAquired() || !left->back->isAccessible) return nullptr;
    if (!left->front.isAquired() || !left->front->isAccessible) return nullptr;

    ChunkHandle leftTop = left->top;
    ChunkHandle leftBot = left->bottom;
    if (!leftTop.isAquired() || !leftTop->isAccessible) return nullptr;
    if (!leftBot.isAquired() || !leftBot->isAccessible) return nullptr;
    if (!leftTop->back.isAquired() || !leftTop->back->isAccessible) return nullptr;
    if (!leftTop->front.isAquired() || !leftTop->front->isAccessible) return nullptr;
    if (!leftBot->back.isAquired() || !leftBot->back->isAccessible) return nullptr;
    if (!leftBot->front.isAquired() || !leftBot->front->isAccessible) return nullptr;

    // right half
    if (!right->back.isAquired() || !right->back->isAccessible) return nullptr;
    if (!right->front.isAquired() || !right->front->isAccessible) return nullptr;

    ChunkHandle rightTop = right->top;
    ChunkHandle rightBot = right->bottom;
    if (!rightTop.isAquired() || !rightTop->isAccessible) return nullptr;
    if (!rightBot.isAquired() || !rightBot->isAccessible) return nullptr;
    if (!rightTop->back.isAquired() || !rightTop->back->isAccessible) return nullptr;
    if (!rightTop->front.isAquired() || !rightTop->front->isAccessible) return nullptr;
    if (!rightBot->back.isAquired() || !rightBot->back->isAccessible) return nullptr;
    if (!rightBot->front.isAquired() || !rightBot->front->isAccessible) return nullptr;

    if (!top->back.isAquired() || !top->back->isAccessible) return nullptr;
    if (!top->front.isAquired() || !top->front->isAccessible) return nullptr;

    if (!bottom->back.isAquired() || !bottom->back->isAccessible) return nullptr;
    if (!bottom->front.isAquired() || !bottom->front->isAccessible) return nullptr;

    // TODO(Ben): Recycler
    ChunkMeshTask* meshTask = new ChunkMeshTask;
 //   meshTask->init(chunk, MeshTaskType::DEFAULT, m_cmp->blockPack, m_cmp->chunkMeshManager);

    // Set dependencies
    meshTask->chunk.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_LEFT] = left.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_RIGHT] = right.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_FRONT] = chunk->front.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_BACK] = chunk->back.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_TOP] = chunk->top.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_BOT] = chunk->bottom.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_LEFT_BACK] = left->back.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_LEFT_FRONT] = left->front.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_LEFT_TOP] = leftTop.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_LEFT_BOT] = leftBot.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_LEFT_TOP_BACK] = leftTop->back.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_LEFT_TOP_FRONT] = leftTop->front.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_LEFT_BOT_BACK] = leftBot->back.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_LEFT_BOT_FRONT] = leftBot->front.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_RIGHT_BACK] = right->back.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_RIGHT_FRONT] = right->front.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_RIGHT_TOP] = rightTop.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_RIGHT_BOT] = rightBot.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_RIGHT_TOP_BACK] = rightTop->back.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_RIGHT_TOP_FRONT] = rightTop->front.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_RIGHT_BOT_BACK] = rightBot->back.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_RIGHT_BOT_FRONT] = rightBot->front.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_TOP_BACK] = top->back.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_TOP_FRONT] = top->front.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_BOT_BACK] = bottom->back.acquire();
    meshTask->neighborHandles[NEIGHBOR_HANDLE_BOT_FRONT] = bottom->front.acquire();

    return meshTask;
}

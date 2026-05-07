// resolver.h
#pragma once
#include <algorithm>
#include <cmath>

class Resolver {
public:
    struct Player {
        float eyeAngles[3];      // Current eye angles (pitch, yaw, roll)
        float lowerBodyYaw;      // Lower body yaw (updated on movement)
        float lastMovingLBY;     // Last LBY when moving
        float lastStandingLBY;   // Last LBY when standing still
        float resolvedAngle;      // Last resolved angle
        float shotTime;          // Time since last shot at this player
        int missedShots;         // Consecutive missed shots
        int hitCount;            // Successful hits
        int resolverMode;        // Current resolver mode (0-4)
        bool isMoving;           // Whether player is moving
        bool isFakewalking;      // Detected fakewalk
        float velocity;          // Current velocity
        float lastUpdateTime;    // Last angle update
    };
    
    Player players[64]; // Max 64 players in CS:GO
    
    enum class Hitbox {
        Head = 0,
        Pelvis = 1,
        Chest = 2
    };
    
    struct AimPoint {
        float x, y, z;
        float priority;
        Hitbox hitbox;
    };
    
    Resolver() {
        // Initialize all players
        for (int i = 0; i < 64; i++) {
            memset(&players[i], 0, sizeof(Player));
            players[i].resolverMode = 0;
            players[i].resolvedAngle = -1.0f;
        }
    }
    
    // Core resolution logic - call this on each frame
    float ResolveYaw(Player& player) {
        float resolved = player.lowerBodyYaw;
        
        // Detect movement state
        player.isMoving = player.velocity > 5.0f;
        
        // Handle fakewalk detection (high velocity with stuttering movement)
        player.isFakewalking = (player.velocity > 10.0f && player.velocity < 80.0f) && 
                                (fabs(player.eyeAngles[1] - player.lowerBodyYaw) < 30.0f);
        
        if (player.isMoving && !player.isFakewalking) {
            // Moving players update LBY frequently
            resolved = ResolveMovingPlayer(player);
        } else {
            // Standing still - LBY updates every ~1.1 seconds
            resolved = ResolveStandingPlayer(player);
        }
        
        // Apply resolver mode based on missed shots
        player.resolvedAngle = ApplyResolverMode(player, resolved);
        
        return player.resolvedAngle;
    }
    
private:
    float ResolveMovingPlayer(Player& player) {
        // Moving players usually have LBY matching their real yaw
        if (fabs(player.eyeAngles[1] - player.lowerBodyYaw) > 35.0f) {
            // Anti-aim detected - LBY lags behind
            return player.lowerBodyYaw + 180.0f;
        }
        return player.lowerBodyYaw;
    }
    
    float ResolveStandingPlayer(Player& player) {
        // Standing players update LBY every 1.1 seconds
        float currentTime = GetCurrentTime();
        float timeSinceUpdate = currentTime - player.lastUpdateTime;
        
        if (timeSinceUpdate > 1.1f) {
            // LBY just updated - this is likely their real angle
            player.lastStandingLBY = player.lowerBodyYaw;
            player.lastUpdateTime = currentTime;
            return player.lowerBodyYaw;
        } else {
            // Between updates - use last known good angle or invert
            if (player.resolvedAngle < 0) {
                return player.eyeAngles[1] + 180.0f;
            }
            return player.resolvedAngle;
        }
    }
    
    float ApplyResolverMode(Player& player, float baseAngle) {
        // Adjust resolver strategy based on missed shots
        switch (player.missedShots % 4) {
            case 0: // Default LBY-based
                return baseAngle;
            case 1: // Opposite angle
                return baseAngle + 180.0f;
            case 2: // Delta adjustment (+45)
                return baseAngle + 45.0f;
            case 3: // Delta adjustment (-45)
                return baseAngle - 45.0f;
            default:
                return baseAngle;
        }
    }
    
    float GetCurrentTime() {
        // Implement with your game's globals->curtime
        return 0.0f;
    }
};

// Shot validation system
class ShotValidator {
private:
    struct ShotRecord {
        float shotTime;
        float aimAngle;
        bool hitConfirmed;
        float hitChance;
        int targetIndex;
    };
    
    std::vector<ShotRecord> recentShots;
    Resolver* resolver;
    
public:
    ShotValidator(Resolver* r) : resolver(r) {}
    
    void OnShot(int targetIndex, float aimAngle, float hitChance) {
        ShotRecord shot;
        shot.shotTime = GetCurrentTime();
        shot.aimAngle = aimAngle;
        shot.hitConfirmed = false;
        shot.hitChance = hitChance;
        shot.targetIndex = targetIndex;
        recentShots.push_back(shot);
        
        // Track missed shot
        resolver->players[targetIndex].missedShots++;
        resolver->players[targetIndex].shotTime = shot.shotTime;
    }
    
    void OnHit(int targetIndex) {
        for (auto& shot : recentShots) {
            if (shot.targetIndex == targetIndex && !shot.hitConfirmed) {
                shot.hitConfirmed = true;
                resolver->players[targetIndex].hitCount++;
                
                // Reset missed shots on successful hit
                resolver->players[targetIndex].missedShots = 0;
                
                // This angle worked - lock onto it temporarily
                resolver->players[targetIndex].resolverMode = 0;
                break;
            }
        }
        
        // Clean old shots (> 2 seconds)
        CleanOldShots();
    }
    
private:
    void CleanOldShots() {
        float currentTime = GetCurrentTime();
        recentShots.erase(
            std::remove_if(recentShots.begin(), recentShots.end(),
                [currentTime](const ShotRecord& shot) {
                    return currentTime - shot.shotTime > 2.0f;
                }),
            recentShots.end()
        );
    }
    
    float GetCurrentTime() { return 0.0f; }
};

// Advanced aim point selection
class AimPointSelector {
public:
    std::vector<Resolver::AimPoint> GetValidAimPoints(Resolver::Player& player, 
                                                       float* headPos, 
                                                       float* pelvisPos) {
        std::vector<Resolver::AimPoint> points;
        
        // Head point (default)
        Resolver::AimPoint headPoint;
        headPoint.x = headPos[0];
        headPoint.y = headPos[1];
        headPoint.z = headPos[2];
        headPoint.hitbox = Resolver::Hitbox::Head;
        
        // Calculate head priority based on resolver confidence
        headPoint.priority = CalculateHeadPriority(player);
        points.push_back(headPoint);
        
        // Body point (safer, higher chance to hit)
        Resolver::AimPoint bodyPoint;
        bodyPoint.x = pelvisPos[0];
        bodyPoint.y = pelvisPos[1];
        bodyPoint.z = pelvisPos[2];
        bodyPoint.hitbox = Resolver::Hitbox::Pelvis;
        bodyPoint.priority = 0.8f;
        points.push_back(bodyPoint);
        
        // Sort by priority (highest first)
        std::sort(points.begin(), points.end(),
            [](const Resolver::AimPoint& a, const Resolver::AimPoint& b) {
                return a.priority > b.priority;
            });
            
        return points;
    }
    
private:
    float CalculateHeadPriority(Resolver::Player& player) {
        float priority = 1.0f;
        
        // Reduce priority after missed shots
        priority *= (1.0f - (player.missedShots * 0.1f));
        
        // Increase priority if we've hit them before
        priority *= (1.0f + (player.hitCount * 0.05f));
        
        // Cap between 0.2 and 1.0
        return std::max(0.2f, std::min(1.0f, priority));
    }
};

// Main integration example
class RageBot {
private:
    Resolver resolver;
    ShotValidator validator;
    AimPointSelector aimSelector;
    
public:
    RageBot() : validator(&resolver) {}
    
    void OnCreateMove(int targetIndex, float* viewAngles) {
        Resolver::Player& target = resolver.players[targetIndex];
        
        // Update target info (you need to get this from game)
        GetPlayerInfo(targetIndex, target);
        
        // Resolve target's real angle
        float resolvedYaw = resolver.ResolveYaw(target);
        
        // Calculate aim point
        float headPos[3], pelvisPos[3];
        GetHitboxPosition(targetIndex, 0, headPos);    // Head hitbox
        GetHitboxPosition(targetIndex, 2, pelvisPos);  // Pelvis hitbox
        
        auto aimPoints = aimSelector.GetValidAimPoints(target, headPos, pelvisPos);
        
        // Select best aim point
        Resolver::AimPoint bestPoint = aimPoints[0];
        
        // Calculate angles to aim point
        float aimAngles[3];
        CalculateAnglesToPoint(bestPoint.x, bestPoint.y, bestPoint.z, aimAngles);
        
        // Apply resolved yaw if headshot
        if (bestPoint.hitbox == Resolver::Hitbox::Head) {
            aimAngles[1] = resolvedYaw;
        }
        
        // Validate shot before shooting
        float hitChance = CalculateHitChance(target, aimAngles);
        if (hitChance > MIN_HIT_CHANCE) {
            // Apply angles
            viewAngles[0] = aimAngles[0];
            viewAngles[1] = aimAngles[1];
            
            // Register shot for validation
            if (IsShooting()) {
                validator.OnShot(targetIndex, resolvedYaw, hitChance);
            }
        }
    }
    
    void OnBulletImpact(int targetIndex) {
        validator.OnHit(targetIndex);
    }
    
private:
    const float MIN_HIT_CHANCE = 35.0f;
    
    void GetPlayerInfo(int index, Resolver::Player& player) {
        // Implement: Get current angles, LBY, velocity from game memory
        // Example (pseudo-code):
        // player.eyeAngles[0] = entity->GetEyeAnglesX();
        // player.eyeAngles[1] = entity->GetEyeAnglesY();
        // player.lowerBodyYaw = entity->GetLowerBodyYaw();
        // player.velocity = entity->GetVelocity().Length2D();
    }
    
    void GetHitboxPosition(int index, int hitboxIndex, float* outPos) {
        // Implement: Get hitbox position from game
    }
    
    void CalculateAnglesToPoint(float x, float y, float z, float* outAngles) {
        // Implement: Angle calculation
        float delta[3] = { x - localPlayerX, y - localPlayerY, z - localPlayerZ };
        outAngles[0] = atan2(-delta[2], hypot(delta[0], delta[1])) * 180.0f / M_PI;
        outAngles[1] = atan2(delta[1], delta[0]) * 180.0f / M_PI;
        outAngles[2] = 0.0f;
        
        // Normalize angles
        outAngles[0] = std::fmod(outAngles[0], 360.0f);
        outAngles[1] = std::fmod(outAngles[1], 360.0f);
    }
    
    float CalculateHitChance(Resolver::Player& target, float* aimAngles) {
        // Implement: Calculate hit chance based on spread, aimbot settings, etc.
        float baseChance = 85.0f;
        
        // Reduce chance if we've been missing
        baseChance -= (target.missedShots * 5.0f);
        
        // Increase if we've been hitting
        baseChance += (target.hitCount * 3.0f);
        
        return std::max(25.0f, std::min(100.0f, baseChance));
    }
    
    bool IsShooting() {
        // Implement: Check if attack key is held
        return false;
    }
};

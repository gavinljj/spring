/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _CANNON_H
#define _CANNON_H

#include "Weapon.h"

class CCannon: public CWeapon
{
	CR_DECLARE_DERIVED(CCannon)

protected:
	/// cached input for GetWantedDir
	float3 lastTargetVec;
	/// cached result for GetWantedDir
	float3 lastLaunchDir = -UpVector;

	/// this is used to keep range true to range tag
	float rangeFactor = 1.0f;

	/// projectile gravity
	float gravity = 0.0f;

	/// indicates high trajectory on/off state
	bool highTrajectory = false;

public:
	CCannon(CUnit* owner = nullptr, const WeaponDef* def = nullptr): CWeapon(owner, def) {}

	void Init() override final;
	void UpdateRange(const float val) override final;
	void UpdateWantedDir() override final;
	void SlowUpdate() override final;

	float GetRange2D(float yDiff, float rFact) const;
	float GetRange2D(const float yDiff) const override final;

private:
	/// tells where to point the gun to hit the point at pos+diff
	float3 GetWantedDir(const float3& diff);
	float3 CalcWantedDir(const float3& diff) const;

	const float3& GetAimFromPos(bool useMuzzle = false) const override { return weaponMuzzlePos; }

	bool HaveFreeLineOfFire(const float3 srcPos, const float3 tgtPos, const SWeaponTarget& trg) const override final;
	void FireImpl(const bool scriptCall) override final;
};

#endif // _CANNON_H

// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

// Custom serialization version for changes made in Fortnite-Dev-Physics stream
struct CORE_API FExternalPhysicsCustomObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,

		// Format change, Removed Convex Hulls From Triangle Mesh Implicit Object
		RemovedConvexHullsFromTriangleMeshImplicitObject,

		// Add serialization for particle bounds
		SerializeParticleBounds,

		// Add BV serialization for evolution
		SerializeEvolutionBV,

		// Allow evolution to swap acceleration structures
		SerializeEvolutionGenericAcceleration,

		//Global elements have bounds so we can skip them
		GlobalElementsHaveBounds,

		//SpatialIdx serialized
		SpatialIdxSerialized,

		//Save out heightfield data
		HeightfieldData,

		//Save out multiple acceleration structures
		SerializeMultiStructures,

		// Add kinematic targets to TKinematicGeometryParticles
		KinematicTargets,
		
		// Allow trimeshes to serialize their acceleration structure
		TrimeshSerializesBV,

		// Serialize broadphase type
		SerializeBroadphaseType,

		// Allow scaled geometry to be a concrete type
		ScaledGeometryIsConcrete,

		// Trimeshes serialize AABBTree
		TrimeshSerializesAABBTree,
		
		// Adds Serialization of HashResult, and separates delete/update TAccelerationStructureHandle in FPendingSpatialData
		SerializeHashResult,

		// Only serialize internal acceleration structure queue and acceleration structure. No external/Async queues.
		FlushEvolutionInternalAccelerationQueue,

		// Serialize world space bounds of TPerShapeData
		SerializeShapeWorldSpaceBounds,

		// Add an SOA which contains particles with full dynamic data but which are in the kinematic object state.
		AddDynamicKinematicSOA,

		// Added material manager to Chaos
		AddedMaterialManager,

		// Added material indices to trimesh collision data
		AddTrimeshMaterialIndices,

		// Add center of mass and volume cached calculations to TConvex
		AddConvexCenterOfMassAndVolume,

		// Add mass transform data to kinematic particle
		KinematicCentersOfMass,

		// Added ability to remove shapes from collision resolution (will not construct constraints when one or more shapes removed)
		AddShapeCollisionDisable,

		//Heightfield cell bounds are implicit
		HeightfieldImplicitBounds,

		// Add damping to rigid particles
		AddDampingToRigids,

		//Replace TBox with TAABB in many places
		TBoxReplacedWithTAABB,

		// Serialize bSimulate on PerShapeData
		SerializePerShapeDataSimulateFlag,

		// Serialize whether or not an AABBTree is immutable
		ImmutableAABBTree,

		// Trimeshes can now use small indices
		TrimeshCanUseSmallIndices,

		// Union objects can avoid allocating a full hierarchy
		UnionObjectsCanAvoidHierarchy,

		// Capsules no longer have a union inside them or stored aabbs
		CapsulesNoUnionOrAABBs,

		// Convexes use concrete planes
		ConvexUsesTPlaneConcrete,

		// Heightfield uses uint16 heights directly
		HeightfieldUsesHeightsDirectly,

		// TriangleMesh has map from internal face indices to external.
		TriangleMeshHasFaceIndexMap,

		// Acceleration structures use unique payload idx
		UniquePayloadIdx,
	
		// Added serialization for the collision type in the shape
		SerializeCollisionTraceType,

		// Force rebuild of indices in BodySetup
		ForceRebuildBodySetupIndices,

		// Added serialization for the physics material sleep counter threshold
		PhysicsMaterialSleepCounterThreshold,

		// Added ability to remove shapes from sim and/or query separately
		AddShapeSimAndQueryCollisionEnabled,

		// Remove extra representations of per shape sim and query enabled flags
		RemoveShapeSimAndQueryDuplicateRepresentations,

		// Removed unused full bounds from AABBTree
		RemovedAABBTreeFullBounds,

		// Added one-way interaction flag
		AddOneWayInteraction,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FExternalPhysicsCustomObjectVersion() {}
};

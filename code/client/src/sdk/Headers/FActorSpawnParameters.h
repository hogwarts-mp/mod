#pragma once

#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include "ESpawnActorCollisionHandlingMethod.h"

///* Struct of optional parameters passed to SpawnActor function(s). */
struct FActorSpawnParameters {
    // FActorSpawnParameters();

    /* A name to assign as the Name of the Actor being spawned. If no value is specified, the name of the spawned Actor
     * will be automatically generated using the form [Class]_[Number]. */
    FName Name;

    /* An Actor to use as a template when spawning the new Actor. The spawned Actor will be initialized using the
     * property values of the template Actor. If left NULL the class default object (CDO) will be used to initialize the
     * spawned Actor. */
    AActor *Template;

    /* The Actor that spawned this Actor. (Can be left as NULL). */
    AActor *Owner;

    /* The APawn that is responsible for damage done by the spawned Actor. (Can be left as NULL). */
    APawn *Instigator;

    /* The ULevel to spawn the Actor in, i.e. the Outer of the Actor. If left as NULL the Outer of the Owner is used. If
     * the Owner is NULL the persistent level is used. */
    class ULevel *OverrideLevel;

#if WITH_EDITOR
    /* The UPackage to set the Actor in. If left as NULL the Package will not be set and the actor will be saved in the
     * same package as the persistent level. */
    class UPackage *OverridePackage;

    /* The parent component to set the Actor in. */
    class UChildActorComponent *OverrideParentComponent;

    /** The Guid to set to this actor. Should only be set when reinstancing blueprint actors. */
    FGuid OverrideActorGuid;
#endif

    /** Method for resolving collisions at the spawn point. Undefined means no override, use the actor's setting. */
    ESpawnActorCollisionHandlingMethod SpawnCollisionHandlingOverride;

  private:
    friend class UPackageMapClient;

    /* Is the actor remotely owned. This should only be set true by the package map when it is creating an actor on a
     * client that was replicated from the server. */
    uint8_t bRemoteOwned : 1;

  public:
    bool IsRemoteOwned() const {
        return bRemoteOwned;
    }

    /* Determines whether spawning will not fail if certain conditions are not met. If true, spawning will not fail
     * because the class being spawned is `bStatic=true` or because the class of the template Actor is not the same as
     * the class of the Actor being spawned. */
    uint8_t bNoFail : 1;

    /* Determines whether the construction script will be run. If true, the construction script will not be run on the
     * spawned Actor. Only applicable if the Actor is being spawned from a Blueprint. */
    uint8_t bDeferConstruction : 1;

    /* Determines whether or not the actor may be spawned when running a construction script. If true spawning will fail
     * if a construction script is being run. */
    uint8_t bAllowDuringConstructionScript : 1;

#if WITH_EDITOR
    /* Determines whether the begin play cycle will run on the spawned actor when in the editor. */
    uint8_t bTemporaryEditorActor : 1;

    /* Determines whether or not the actor should be hidden from the Scene Outliner */
    uint8_t bHideFromSceneOutliner : 1;

    /** Determines whether to create a new package for the actor or not. */
    uint16_t bCreateActorPackage : 1;
#endif

    /* Modes that SpawnActor can use the supplied name when it is not None. */
    enum class ESpawnActorNameMode : uint8 {
        /* Fatal if unavailable, application will assert */
        Required_Fatal,

        /* Report an error return null if unavailable */
        Required_ErrorAndReturnNull,

        /* Return null if unavailable */
        Required_ReturnNull,

        /* If the supplied Name is already in use the generate an unused one using the supplied version as a base */
        Requested
    };

    /* In which way should SpawnActor should treat the supplied Name if not none. */
    ESpawnActorNameMode NameMode;

    /* Flags used to describe the spawned actor/object instance. */
    EObjectFlags ObjectFlags;
};

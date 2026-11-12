# Unreal Engine PCG Framework: Architecture Report

Date: 2026-10-25. Research only, no design for our engine.

**Source studied:** `F:\Engine\Engine\Plugins\PCG` (`PCG.uplugin`: `"Version": 8`, `"VersionName": "1.0"`, not beta).
The engine is **UE 5.8.0** (`F:\Engine\Engine\Build\Build.version`: Major 5, Minor 8, Patch 0, branch `UE5`).
Related plugins were checked too: `Plugins/PCGInterops/*` (GeometryScript, Python, ExternalData),
`Plugins/Experimental/PCGInterops/*` (Water, Niagara, FastGeo, InstancedActors, NaniteAssemblies),
`Plugins/Experimental/PCGMeshPartitionInterop`, and `Plugins/Experimental/PCGBiomeCore`.

Unless noted otherwise, paths below are relative to `F:\Engine\Engine\Plugins\PCG\Source\PCG\`.
`Public/`, `Private/` and `Internal/` are the module folders.

---

## 1. Core objects and how they relate

### UPCGComponent: the execution source
- `Public/PCGComponent.h`. `UPCGComponent : UActorComponent, IPCGGraphExecutionSource`. In 5.8 the
  executor talks to an abstract `IPCGGraphExecutionSource` (`Public/PCGGraphExecutionStateInterface.h`,
  `Public/PCGDefaultExecutionSource.h`), and the component is the main implementation of it.
- It is placed on any actor. `APCGVolume : AVolume` (`Public/PCGVolume.h`) is a box volume that carries a
  PCG component. `APCGPartitionActor` (`Public/Grid/PCGPartitionActor.h`) holds **local components**,
  which are copies of a partitioned "original" component, one per grid cell.
- The component stores a graph instance, not a raw graph: `UPCGGraphInstance* GetGraphInstance()`
  (`PCGComponent.h:189`). `UPCGGraphInstance::CreateInstance(Owner, Graph)` is in `PCGGraph.h:922`.
- Key properties (`PCGComponent.h:313-367`):
  - `int Seed = 42`, `bActivated`, `bIsComponentPartitioned`.
  - `EPCGComponentGenerationTrigger GenerationTrigger = GenerateOnLoad`.
  - `bRegenerateInEditor`, `bOnlyTrackItself`, `bIgnoreLandscapeTracking`, `TrackingPriority`.
  - Editing mode `EPCGEditorDirtyMode` (Normal / Preview / LoadAsPreview, `Public/PCGCommon.h:588`).
    In Preview mode, generation does not dirty the level and generated resources are not saved.
- Generated output is tracked in `FPCGManagedResourceContainer ManagedResourceContainer`
  (`PCGComponent.h:765-770`, `Public/PCGManagedResourceContainer.h`). Input data is cached on the
  component: `CachedInputData`, `CachedActorData`, `CachedLandscapeData`, `CachedLandscapeHeightData`
  (`PCGComponent.h:749-761`).

### UPCGGraph, UPCGGraphInterface, UPCGGraphInstance
- `Public/PCGGraph.h`. The abstract base is `UPCGGraphInterface` (line 159), which exposes
  `GetUserParametersStruct()` and typed `GetGraphParameter<T>` / `SetGraphParameter<T>`.
- `UPCGGraph : UPCGGraphInterface` (line 332) holds:
  - `TArray<UPCGNode*> Nodes`, plus dedicated `InputNode` and `OutputNode`.
  - `FInstancedPropertyBag UserParameters`, the graph's typed parameter schema.
  - `bLandscapeUsesMetadata`, `bIgnoreLandscapeTracking`.
  - `bUseHierarchicalGeneration`, `HiGenGridSize = Grid256`, `HiGenGridSizeMultiplier`.
  - `EmbeddedSubgraphs`.
- `UPCGGraphInstance` (around line 958) holds a `Graph` (any interface, so instances can chain) and
  `FPCGOverrideInstancedPropertyBag ParametersOverrides`. That is a property bag plus per-property
  "overridden" flags (`PCGGraph.h:77-117`), so an instance overrides only the parameters it touches.
- At execution time the parameters are frozen into a `UPCGUserParametersData`
  (`ConstructUserParametersData`, `PCGGraph.h:187`; `Public/Data/PCGUserParametersData.h`).
  Nodes read them with **Get Graph Parameter** (`Public/Elements/PCGUserParameterGet.h`).

### UPCGNode, UPCGSettings, IPCGElement (the node/settings/element split)
- `UPCGNode` (`Public/PCGNode.h:26`) is graph topology only: a `SettingsInterface`, plus
  `InputPins`/`OutputPins` of type `UPCGPin` (`Public/PCGPin.h`). A pin holds `Edges` and
  `FPCGPinProperties` (Label, AllowedTypes, bAllowMultipleData, Usage).
- `UPCGSettings` (`Public/PCGSettings.h:267`) is the node's authored data: UPROPERTYs, pins
  (`InputPinProperties`/`OutputPinProperties`), `Seed`, and `bEnabled`/`bDebug` (from
  `UPCGSettingsInterface`, line 179). `UPCGSettingsInstance` (line 762) lets a node share another
  node's settings.
- The element is the stateless executor. `UPCGSettings::CreateElement()` is pure virtual
  (`PCGSettings.h:529`). `GetElement()` creates the element lazily **once per settings object** and
  caches it (`Private/PCGSettings.cpp:693-706`).
- `IPCGElement` (`Public/PCGElement.h:139`) runs in phases: `PrepareDataInternal` → `ExecuteInternal` →
  `PostExecuteInternal`, with `AbortInternal` on cancel. The phase enum `EPCGExecutionPhase` is in
  `Public/PCGContext.h:53`. Elements also declare `IsCacheable`, `CanExecuteOnlyOnMainThread`,
  `GetDependenciesCrc` and `IsPassthrough`.
- Per-execution state lives in `FPCGContext` (`Public/PCGContext.h`): input and output collections,
  the settings, and the execution source. Custom contexts come from `IPCGElementWithCustomContext<T>`.
  Time-sliced elements derive from `Public/Elements/PCGTimeSlicedElementBase.h`.

### The UPCGData hierarchy
`Public/PCGData.h`, `Public/Data/*.h`, `Public/PCGParamData.h`:

```
UPCGData
├─ UPCGSpatialData                    (Data/PCGSpatialData.h: GetBounds, GetDensityAtPosition, SamplePoint,
│  │                                   ProjectPoint, ToPointData, IntersectWith/ProjectOn/UnionWith/Subtract)
│  ├─ UPCGBasePointData
│  │  ├─ UPCGPointArrayData           (SoA value ranges; default, cvar pcg.EnablePointArrayData=true, PCGContext.cpp:32)
│  │  └─ UPCGPointData                (legacy TArray<FPCGPoint>)
│  ├─ UPCGSpatialDataWithPointCache
│  │  ├─ UPCGSurfaceData → UPCGLandscapeData, UPCGSplineInteriorSurfaceData, UPCGPolygon2DInteriorSurfaceData,
│  │  │                     UPCGTexture2DBaseData → UPCGTextureData / UPCGRenderTargetData, UPCGVirtualTextureData,
│  │  │                     UPCGWorldRayHitData
│  │  ├─ UPCGPolyLineData → UPCGSplineData, UPCGLandscapeSplineData, UPCGPolygon2DData
│  │  ├─ UPCGVolumeData → UPCGWorldVolumetricData
│  │  ├─ UPCGPrimitiveData, UPCGCollisionShapeData
│  │  └─ composites: UPCGIntersectionData, UPCGUnionData, UPCGDifferenceData, UPCGProjectionData (→ UPCGSplineProjectionData)
│  ├─ UPCGDynamicMeshData, UPCGCollisionWrapperData
├─ UPCGParamData                       (attribute set, no spatial meaning)
├─ UPCGUserParametersData, UPCGResourceData → UPCGStaticMeshResourceData, UPCGDataViewData
└─ UPCGSettingsInterface              (settings are data too, which enables settings overrides via pins)
```

- **Concrete vs composite.** Spatial operators produce lazy composite data. Sampling evaluates the
  network on demand (`SamplePoint`), and `ToPointData` / "To Point" / "Make Concrete" collapse it to
  points (`PCGSpatialData.h:169-253`, `RequiresCollapseToSample`).
- **Points.** `FPCGPoint` (`Public/PCGPoint.h`) holds Transform, Density, BoundsMin/BoundsMax, Color,
  Steepness, `int32 Seed` and `int64 MetadataEntry`. Properties are addressable by name through
  `EPCGPointProperties`.
- **Data on the wire.** Data moves between nodes as `FPCGTaggedData { Data, TSet<FString> Tags, FName Pin }`
  inside an `FPCGDataCollection` (`PCGData.h:199-219`). Tags travel with each data object, not with
  each point. Tag nodes include Add Tags, Delete Tags, Replace Tags, Filter Data By Tag and
  Get Tags / Data Tags To Attribute Set.

### Metadata and attributes
- `UPCGMetadata` (`Public/Metadata/PCGMetadata.h:53`) is a per-data attribute store. It has a
  **parent chain**: `Initialize(const UPCGMetadata* InParent)`, so a child inherits its parent's
  attributes without copying them. Each point refers to its entry through `MetadataEntry`.
- **Domains** (5.5+): `PCGMetadataDomainID::Default`, `Elements` (per point) and `Data` (per data
  object) (`Public/Metadata/PCGMetadataCommon.h:120-125`).
- Attribute value types (`PCGMetadataCommon.h:225`): Float, Double, Integer32, Integer64, Vector2,
  Vector, Vector4, Quaternion, Transform, String, Boolean, Rotator, Name, SoftObjectPath,
  SoftClassPath. Hidden newer types: Byte, Text, Enum, Struct, Object.
- Nodes address points and attributes uniformly with `FPCGAttributePropertySelector`
  (`Public/Metadata/PCGAttributePropertySelector.h`), e.g. `$Position.Z`, `$Density` or `MyAttr`.

---

## 2. Execution model

### Scheduling, compiling and executing
- `UPCGComponent::GenerateInternal` checks `ShouldGenerate(bForce, trigger)` and then calls
  `UPCGSubsystem::ScheduleComponent` (`Private/PCGComponent.cpp:544-577`).
- `CreateGenerateTask` (`PCGComponent.cpp:579-625`) works like this:
  - If the component was already generated, it first queues `CleanupLocal(bRemoveComponents=false)`,
    a soft cleanup (see §3).
  - It computes `GetGridBounds()` and schedules the graph with the cleanup task as a dependency.
- `FPCGGraphExecutor` (`Private/Graph/PCGGraphExecutor.h:46`) is the shared work queue:
  `Schedule`, `ScheduleGeneric`, and `Cancel` by source or graph. It runs elements, time-slices them,
  and moves work between worker threads and the main thread.
- `FPCGGraphCompiler` (`Private/Graph/PCGGraphCompiler.h:54`) flattens a graph into
  `TArray<FPCGGraphTask>` per generation grid size (`GetCompiledTasks(Graph, GridSize, ...)`).
  Static subgraphs are inlined. `CullTasks` and `Recompile` are part of the same class.
  `FPCGStackContext` (`Public/Graph/PCGStackContext.h`) records the subgraph call stack for
  inspection and debugging.
- Before each task runs, the executor builds the task's input. It computes a CRC for each input data
  item, covering data CRC, tags, output pin label and input pin label
  (`PCGElement.cpp:1371`, `PCGGraphExecutor.cpp:~2711-2744`).

### Element result caching (CRC-based)
- **Cache key.** `FPCGCacheEntryKey(const IPCGElement*, FPCGCrc)` (`Private/Graph/PCGGraphCache.h:26-53`).
  The key is the element pointer plus a dependencies CRC. Because elements are per settings object,
  this works out to one cache line per node and input combination.
- **Dependencies CRC.** The default `IPCGElement::GetDependenciesCrc` (`Private/PCGElement.cpp:1355-1400`)
  starts from prime 1000003 and combines:
  - every input data CRC;
  - `Settings->GetSettingsCrc()`, a serialized-property CRC built by `FPCGSettingsObjectCrc::PCGCrc`
    (`PCGSettings.cpp:1360-1393`); properties tagged `meta=(PCGNoHash)` are excluded;
  - the execution source seed, **only if `Settings->UseSeed()`**;
  - the source data container, if one is present.
- **Data CRC.** `UPCGData::ComputeCrc` / `AddToCrc` (`Public/PCGData.h:165-176`). Data types without a
  bespoke CRC fall back to their unique object UID. Elements opt into a full content CRC with
  `ShouldComputeFullOutputDataCrc`.
- **Cache store.** `FPCGGraphCache` (`PCGGraphCache.h:67`) is an LRU cache with a memory budget
  (`EnforceMemoryBudget`). `CleanFromCache(Element)` evicts one element.
- **What is not cached.** GPU elements are not cacheable (`IsCacheable` default, `PCGElement.cpp:1321`),
  and neither are disabled settings. Spawners and other world-mutating elements override
  `IsCacheable` to return false.

### Determinism and seeds
- `PCGHelpers::ComputeSeed(A)`, `(A,B)` and `(A,B,C)` are fixed integer hash mixes
  (`Private/Helpers/PCGHelpers.cpp:35-48`). `ComputeSeedFromPosition` hashes `(int)X, (int)Y, (int)Z`.
- **Component seed.** `UPCGComponent::Seed` (default 42), read through
  `FPCGComponentExecutionState::GetSeed` (`Private/PCGComponentExecutionState.cpp:18`). Partition
  local components copy the original's seed (`PCGComponent.cpp:445`).
- **Node seed.** `UPCGSettings::Seed`. New settings get a default derived from a hash of the class
  name (`PCGSettings.h:480-481`). The effective node seed is
  `UPCGSettings::GetSeed(source) = ComputeSeed(Settings.Seed, ComponentSeed)` when `UseSeed()` is
  true (`PCGSettings.cpp:713-716`). This value is exposed as `FPCGContext::GetSeed()`
  (`PCGContext.cpp:129`).
- **Point seed.** Samplers assign each point a seed:
  - Surface Sampler: `FRandomStream(ComputeSeed(SamplerSeed, cellX, cellY))`
    (`Private/Elements/PCGSurfaceSampler.cpp:281`).
  - World queries: `ComputeSeedFromPosition` (`Private/Data/PCGWorldData.cpp:265`).
  - Spline Sampler: `SeedFromPosition` or `SeedFromIndex` (`EPCGSplineSamplingSeedingMode`).
- **Per-point randomness.** Nodes build a stream from context seed and point seed, e.g.
  Transform Points uses `FRandomStream(ComputeSeed(Context->GetSeed(), PointSeed))`
  (`Private/Elements/PCGTransformPoints.cpp:170`). The helper is
  `PCGHelpers::GetRandomStreamFromSeed(seed, settings, source)`, which combines all three
  (`PCGHelpers.cpp:54-70`). Transform Points can optionally re-seed points from their new position
  (`bRecomputeSeed`, `PCGTransformPoints.cpp:239-242`).
- **Result:** output is a pure function of inputs, settings, component seed, node seed and point
  positions or indices. Moving a point changes its seed when the seed comes from its position.

### What triggers regeneration (editor)
- `FPCGTrackingManager` (`Internal/PCGTrackingManager.h:53`) is owned by the subsystem. It maps
  original components to local ones and does change tracking. It listens to actor
  added/deleted/loaded/unloaded, `OnObjectModified`, `OnObjectTransacted` and level instance edits
  (lines ~110-135). `PCGEditor/Internal/ChangeTracking/PCGActorTracker.h` is the editor-side tracker.
- **Tracking keys.** `FPCGSelectionKey` (`Public/Elements/PCGActorSelector.h:151-165`) is keyed by
  actor filter, tag or class. Get Actor Data-style nodes register keys statically or dynamically
  (`UPCGComponent::RegisterDynamicTracking`, `PCGComponent.h:472-475`).
  `bTrackActorsOnlyWithinBounds` (`Public/Elements/PCGDataFromActor.h:142`) culls changes outside the
  component bounds.
- **On a matching change**, `ClearCacheForKeys` evicts the cache entries of exactly the settings that
  read that key, then the component is refreshed (`PCGTrackingManager.h`, `ClearCacheForKeys`).
- **Dirty flags.** `EPCGComponentDirtyFlag { Actor, Landscape, Input, Data }` (`PCGComponent.h:85-93`).
  Landscape edits refresh dependents unless `bIgnoreLandscapeTracking` is set on the component or the
  graph. Settings edits notify through `EPCGChangeType` → `FPCGGraphCompiler::NotifyGraphChanged`.
- **Within one generation**, change origins that the generation itself causes are ignored
  (`IgnoreChangeOriginsDuringGenerationWithScope`, `PCGComponent.h:493-520`). This keeps a spawned
  actor from re-triggering its own generation.

### Generate, Cleanup and trigger modes
- **API.** `Generate()`/`Generate(bForce)`, `Cleanup()`/`Cleanup(bRemoveComponents)`,
  `GenerateLocal`, `CleanupLocal`, `CleanupLocalImmediate`, and
  `CleanupLocalDeleteAllGeneratedObjects` (`PCGComponent.h:216-257`). That last one purges actors
  tagged as PCG-generated but no longer owned.
- **`EPCGComponentGenerationTrigger`** (`PCGComponent.h:77-82`):
  - `GenerateOnLoad`: generates when the component loads.
  - `GenerateOnDemand`: only on an explicit call, e.g. the editor Generate button or Blueprint.
  - `GenerateAtRuntime`: driven by the Runtime Generation Scheduler.
- **Delegates:** `OnPCGGraphStartGenerating`, `Cancelled`, `Generated`, `Cleaned`.

---

## 3. Ownership and cleanup: managed resources

- **Base class.** `UPCGManagedResource` (`Public/PCGManagedResource.h:84`). It stores an `FPCGCrc Crc`
  and a transient `bIsMarkedUnused`. Its virtuals:
  - `Release(bHardRelease, OutActorsToDelete)`: returns true if the resource can be dropped.
  - `ReleaseIfUnused`, `MarkAsUsed`, `MarkAsReused`, `MoveResource`, `IsManaging(Object)`.
- **Subclasses** (same file):
  - `UPCGManagedActors`: a set of spawned actors.
  - `UPCGManagedComponentBase` → `UPCGManagedComponent` (one component),
    `UPCGManagedComponentList`, `UPCGManagedComponentDefaultList`.
  - `UPCGManagedISMComponent`: an ISMC plus `FISMComponentDescriptor`, `SettingsCrc`, `DataCrc` and
    reuse counters (lines 351-420).
  - `UPCGManagedISKMComponent`: instanced skinned mesh.
  - `UPCGManagedSplineMeshComponent`.
  - Interop plugins add more, e.g. `UPCGManagedPatchModifier`
    (`Experimental/PCGMeshPartitionInterop/.../PCGManagedMeshPartitionPatchModifier.h`).
- **Registration.** Spawners call `UPCGComponent::AddToManagedResources` (`PCGComponent.cpp:251`).
  ISMCs are found or created through `UPCGActorHelpers::GetOrCreateManagedISMC`
  (`Public/Helpers/PCGActorHelpers.h:133-146`). It looks for an existing managed ISMC on the target
  actor with a matching descriptor and settings, so regeneration **refills the existing component**
  rather than destroying and recreating it.
- **Regeneration cycle**, a mark-and-sweep:
  1. `CreateGenerateTask` → `CleanupLocal(false)` performs a **soft release**
     (`FPCGManagedResourceContainerHelper::Cleanup(bReleaseManagedResources=false)`,
     `Private/PCGManagedResourceContainer.cpp:148-170`). Resources that support reset are marked
     unused rather than destroyed. Components get the tag `PCGHelpers::MarkedForCleanupPCGTag` and
     ISMCs are emptied (`UPCGManagedComponent::Release`, `Private/PCGManagedResource.cpp:602-640`).
     Anything that cannot reset is hard-deleted.
  2. During execution, spawners reuse marked resources and call `MarkAsUsed` or `MarkAsReused`.
  3. `PostProcessGraph` → `CleanupUnusedManagedResources()` (`PCGComponent.cpp:627-635`,
     `PCGManagedResourceContainer.cpp:106-140`) runs `ReleaseIfUnused` on every resource. Resources
     still unused are destroyed and removed, and their actors are batch-deleted
     (`UPCGActorHelpers::DeleteActors`).
- **Full cleanup.** `Cleanup(bRemoveComponents=true)` does a **hard release**: components are
  destroyed and managed actors deleted (`UPCGManagedActors::Release`, `PCGManagedResource.cpp:234-300`).
  Spawned actors that carry their own PCG component get `CleanupLocalImmediate` called recursively
  (line 293).
- **Abort.** On cancel, spawners soft-release the resources they touched, so the sweep removes
  half-written ones (`FPCGStaticMeshSpawnerElement::AbortInternal`,
  `Private/Elements/PCGStaticMeshSpawner.cpp:913-932`).
- **Preview mode.** Resources are marked `bMarkedTransientOnLoad` and hidden rather than mutated, so
  saved levels are not altered (`PCGManagedResource.h:120-147`, `DeletePreviewResources`).

---

## 4. Inputs from the world

### The Input node and the component's own data
- The graph Input node's default pins are named in `Public/PCGInputOutputSettings.h:11-15`:
  `"Input"`, `"Actor"`, `"OriginalActor"`, `"Landscape"` and `"Landscape Height"`.
- `UPCGComponent::CreateActorPCGData` (`PCGComponent.cpp:3155`) builds data from the owning actor
  through `FPCGGetDataFunctionRegistry` (`Private/Data/Registry/PCGGetDataFunctionRegistry.cpp:120-310`).
  It returns a union if the actor yields several items. What each actor type yields:
  - `APCGPartitionActor`: `UPCGVolumeData` of the cell bounds, intersected with the original
    component's actor data.
  - `ALandscapeProxy`: `UPCGLandscapeData`.
  - `AVolume` (including `APCGVolume`): `UPCGVolumeData`.
  - Spline components: `UPCGSplineData`. Landscape spline components: `UPCGLandscapeSplineData`.
  - Primitive components: `UPCGPrimitiveData`.
- The deprecated `EPCGComponentInput {Actor, Landscape, Other}` (`PCGComponent.h:67`) used to
  intersect the actor shape with the landscape (`PCGComponent.cpp:~3328-3347`).

### Landscape
- **`UPCGLandscapeData : UPCGSurfaceData`** (`Public/Data/PCGLandscapeData.h`).
  `FPCGLandscapeDataProps` controls what gets sampled: `bGetHeightOnly`, `bGetLayerWeights`,
  `bGetActorReference`, `bGetPhysicalMaterial`, `bGetComponentCoordinates`,
  `bSampleVirtualTextures`, `bSampleVirtualTextureNormals` (lines 25-52).
- **Sampled values.** A sample yields a point on the surface, oriented to the landscape normal, with
  one float attribute per layer weight (when `bGetLayerWeights` is set; the graph default is
  `bLandscapeUsesMetadata`).
- **Landscape cache.** Values come from `UPCGLandscapeCache` (`Public/Grid/PCGLandscapeCache.h`), stored
  on `APCGWorldActor::LandscapeCacheObject` (`Public/PCGWorldActor.h:67`). Per landscape component it
  stores `PositionsAndNormals` and per-layer `uint8` weights `LayerData` with `LayerDataNames`
  (lines 103-113), serialized into bulk data in editor.
- **Collision path.** Height can also be read from heightfield collision components
  (`Private/Data/PCGLandscapeData.cpp:~167-175`).
- **Related nodes:**
  - **Get Landscape Data** (`UPCGGetLandscapeSettings`, `Public/Elements/PCGTypedGetter.h:14`).
  - **Wait Until Landscape Is Ready** (`Public/Elements/Landscape/PCGWaitLandscapeReady.h`).
  - **Generate Landscape Textures** (GPU height texture and grass maps,
    `Public/Elements/PCGGenerateLandscapeTextures.h:37-38`).
  - **Normal To Density** (`PCGNormalToDensity.h`) for slope filtering.

### Splines
- **`UPCGSplineData : UPCGPolyLineData`** (`Public/Data/PCGSplineData.h`). A closed spline also yields
  an interior surface, `UPCGSplineInteriorSurfaceData` (`Public/Data/PCGSplineInteriorSurfaceData.h`),
  exposed by **Create Surface From Spline**.
- **Spline Sampler** (`Public/Elements/PCGSplineSampler.h`):
  - `Dimension`: `OnSpline`, `OnHorizontal`, `OnVertical`, `OnVolume`, `OnInterior`.
  - `Mode`: `Subdivision`, `Distance`, `NumberOfSamples`.
  - `Fill`: `Fill` or `EdgesOnly`.
  - Interior controls: `InteriorSampleSpacing`, `InteriorBorderSampleSpacing`, `InteriorOrientation`
    (`Uniform`/`FollowCurvature`), `bTreatSplineAsPolyline`, `bProjectOntoSurface`.
  - Optional output attributes: alpha, curvature, segment index, tangents and direction delta.
- **Get Spline Data** (`PCGTypedGetter.h:77`). **Get Spline Control Points**
  (`PCGGetSplineControlPoints.h`).
- **Spline utilities:** Create Spline, Spawn Spline Component, Clean Spline, Split Spline,
  Offset Spline, Spline Intersection, Spline Direction, Subdivide Spline.
- **Water splines:** **Get Water Spline Data** (`Experimental/PCGInterops/PCGWaterInterop`).

### Other actors: Get Actor Data and selectors
- **Get Actor Data** (`UPCGDataFromActorSettings`, `Public/Elements/PCGDataFromActor.h`).
  - It combines an `FPCGActorSelectorSettings` (actor) and an `FPCGComponentSelectorSettings`
    (component) selector.
  - `EPCGGetDataFromActorMode` (lines 17-27): `ParseActorComponents`, `GetSinglePoint`,
    `GetDataFromProperty`, `GetDataFromPCGComponent`, `GetDataFromPCGComponentOrParseComponents`,
    `GetActorReference`, `GetComponentsReference`, `GetPointData`.
  - Flags: `bComponentsMustOverlapSelf`, `bIgnorePCGGeneratedComponents`,
    `bTrackActorsOnlyWithinBounds`, `bAlwaysRequeryActors`.
- **Selector enums** (`Public/Elements/PCGActorSelector.h:24-58`):
  - `EPCGActorSelection { ByTag, ByClass }` (ByName and ByPath are hidden).
  - `EPCGActorFilter { Self, Parent, Root, AllWorldActors, Original, FromInput }`.
  - `EPCGComponentSelection { ByTag, ByClass }`.
  - Options: `bMustOverlapSelf`, `bIncludeChildren`, `bSelectMultiple`, `bIgnoreSelfAndChildren`.
- **Typed getters** that subclass Get Actor Data (`PCGTypedGetter.h`): Get Landscape Data,
  Get Spline Data, Get Volume Data, Get Primitive Data, Get PCG Component Data,
  Get Virtual Texture Data.
- **Get Actor Property** reads a single property. **World Ray Hit Query**, **World Volumetric Query**
  and **World Raycast** (`PCGWorldQuery.h`, `PCGWorldRaycast.h`) sample physics collision as a
  surface or volume.

---

## 5. Node catalogue (exact titles from `GetDefaultNodeTitle`)

Elements are in `Public/Elements/` (`PCG<Name>.h`) unless noted. Items marked **(ENV)** are the most
relevant to environment building.

**Inputs and getters:**
- Get Actor Data (ENV), Get Landscape Data (ENV), Get Spline Data (ENV), Get Volume Data,
  Get Primitive Data, Get PCG Component Data, Get Virtual Texture Data.
- Get Texture Data, Get Texture 2D Array Data, Get Actor Property, Get Graph Parameter (ENV),
  Get Tool Data, Get Bounds, Get Static Mesh Resource Data, Get Spline Control Points.
- Load PCG Data Asset, Load Data Table, Data Table Row To Attribute Set, Get Asset List,
  Get Console Variable, Get Execution Context Info, Get Editor Cameras (editor only,
  `Private/Elements/Editor/`).

**Samplers:**
- Surface Sampler (ENV): `PointsPerSquaredMeter`, `PointExtents`, `Looseness`, `bUnbounded`; pins
  `Surface` and `Bounding Shape` (`PCGSurfaceSampler.h:20-21,135-165`).
- Spline Sampler (ENV), Volume Sampler, Texture Sampler (title Sample Texture), Point From Mesh.
- Mesh Sampler (`PCGInterops/PCGGeometryScriptInterop/.../PCGMeshSampler.h`).
- Create Points, Create Points Grid, Create Points Sphere.
- World Ray Hit Query, World Raycast, World Volumetric Query.

**Filters:**
- Density Filter (ENV): `LowerBound`, `UpperBound`, `bInvertFilter` (`PCGDensityFilter.h:31-37`).
- Filter Attribute Elements (`PCGAttributeFilter.h`), Filter Data By Attribute, Filter Data By Tag (ENV),
  Filter Data By Index, Filter Elements By Index, Filter Data By Type.
- Self Pruning (ENV): `EPCGSelfPruningType { LargeToSmall, SmallToLarge, AllEqual, None, RemoveDuplicates }`,
  `RadiusSimilarityFactor`, `bRandomizedPruning`, plus an optional collision attribute
  (`PCGSelfPruning.h:19-59`).
- Select Points, Cull Points Outside Actor Bounds (ENV), Remove Empty Data, Random Choice, Sanity Check Point Data.

**Spatial operations (shape algebra):**
- Difference (ENV): pins `Source` and `Differences`; `EPCGDifferenceDensityFunction { Minimum, ClampedSubstraction, Binary }`;
  `EPCGDifferenceMode { Inferred, Continuous, Discrete }` (`PCGDifferenceElement.h:12-56`,
  `Data/PCGDifferenceData.h:16-29`).
- Intersection, Inner Intersection, Union, Projection (ENV, pin `Projection Target`).
- Bounds Modifier (ENV, `Set/Intersect/Include/Translate/Scale`), Extents Modifier, Apply Scale To Bounds,
  Bounds From Mesh, Reset Point Center.
- Make Concrete, To Point, Create Collision Data.
- Polygon tools: Create Polygon 2D, Polygon Operation, Offset Polygon, Clip Paths,
  Create Surface From Polygon2D, Create Surface From Spline, Find Convex Hull 2D, Get Segment.

**Point operations:**
- Transform Points (ENV): random offset, rotation and scale ranges, absolute flags, `bUniformScale`,
  `bRecomputeSeed`.
- Copy Points (ENV): pins `Source`/`Target`; rotation, scale and color inheritance
  `Relative/Source/Target`; metadata inheritance `SourceFirst/TargetFirst/SourceOnly...`
  (`PCGCopyPoints.h:11-38`).
- Density Remap; Attribute Noise (ENV, preconfigured as "Density Noise",
  `Private/Elements/PCGAttributeNoise.cpp:104`; modes `Set/Minimum/Maximum/Add/Multiply`).
- Spatial Noise (Perlin2D, Caustic2D, Voronoi2D, ... `PCGSpatialNoise.h:11-19`).
- Distance (ENV), Normal To Density (ENV), Point Neighborhood, Attract, Align Points.
- Combine Points, Collapse Points, Merge Points, Duplicate Point, Split Points, Edit Points.
- Blur, Cluster, Pathfinding, Elevation Isolines, Generate Seed, Mutate Seed, Apply Hierarchy.

**Spawners and world output:**
- Static Mesh Spawner (ENV, `PCGStaticMeshSpawner.h`):
  - `MeshSelectorType` + `MeshSelectorParameters`; `InstanceDataPackerType`.
  - `bApplyMeshBoundsToPoints`, `TargetActor`, `OutAttributeName`, `bAllowMergeDifferentDataInSameInstancedComponents`,
    culling cells.
- Mesh selectors (`Public/MeshSelectors/`): `UPCGMeshSelectorWeighted` (entries of
  `{FPCGSoftISMComponentDescriptor Descriptor, int Weight}`), `UPCGMeshSelectorByAttribute`,
  `UPCGMeshSelectorWeightedByCategory`.
- Spawn Actor (ENV): `EPCGSpawnActorOption { CollapseActors, MergePCGOnly, NoMerging }`; triggers PCG on
  spawned actors per `EPCGSpawnActorGenerationTrigger`; has DataLayer and HLOD settings
  (`PCGSpawnActor.h:17-112`).
- Spawn Spline Mesh (ENV), Spawn Spline Component, Instanced Skinned Mesh Spawner, Create Target Actor,
  Add Component, Apply On Object.
- Remove Actors From World (editor only, `Private/Elements/Editor/PCGRemoveActorsFromWorld.cpp`).
- Copy to Render Target, Save Texture to Asset, Save PCG Data Asset.

**Graph structure and control flow:**
- Subgraph, Loop (`UPCGLoopSettings : UPCGSubgraphSettings`, `PCGLoopElement.h:10`), Get Loop Index,
  Get Subgraph Depth.
- Branch, Select, Select (Multi)/Switch, Boolean Select, Runtime Quality Branch/Select, Platform Switch, Wait.
- Gather, Reroute, Proxy, Execute Blueprint, Custom HLSL (GPU), Set Grid Size / Change Grid Size
  (`Private/Elements/PCGHiGenGridSize.cpp:23-24`).

**Attribute operations (`Public/Elements/Metadata/`):**
- Create Attribute, Copy Attributes, Delete Attributes, Attribute Rename, Attribute Cast,
  Attribute Remap, Attribute Partition, Attribute Reduce, Attribute Select.
- Attribute Maths/Boolean/Compare/Bitwise/Trig/Vector/Rotator/Transform/String Op.
- Make/Break Vector/Transform/Rotator Attribute, Match And Set Attributes, Point Match And Set,
  Merge Attributes, Sort Attributes, Hash Attribute.
- Point To Attribute Set, Attribute Set To Point, Get Attribute From Point Index,
  Extract Attribute at Index, Parse String.
- Tags: Add Tags, Delete Tags, Replace Tags, Get Tags, Data Tags To Attribute Set,
  Replace Data By Tag, Sort Data By Tag Value.

**Shape grammar:** Subdivide Spline, Subdivide Segment, Spline to Segment, Duplicate Cross-Sections,
Select Grammar, Print Grammar (`Public/Elements/Grammar/`).

**Debug:** Debug, Print String, Visualize Attribute.

---

## 6. Writing to the landscape and other world-modifying outputs

This section separates what exists in 5.8.0 from what does not.

- **Classic `ALandscape` heightmaps and weightmaps: no writer node exists in this version.**
  - Every `ALandscape*` reference in the PCG module is a read path: data, cache, typed getter, world
    query, wait-ready and GPU texture generation (grep over `Private/`; the file list is in
    `PCGLandscapeData.cpp`, `PCGLandscapeCache.cpp`, `PCGGenerateLandscapeTextures.cpp`,
    `PCGWaitLandscapeReady.cpp`).
  - **Generate Landscape Textures** only *reads* a height texture and grass maps on the GPU.
  - The `Editor/LandscapePatch` plugin (`LandscapeCircleHeightPatch`, `LandscapeTexturePatch`,
    `LandscapePatchEditLayer`) has **no PCG references** in its source. A graph could Spawn Actor a
    Blueprint that contains a landscape patch component, but no node or code in PCG targets this
    directly.
  - The Experimental **PCGBiomeCore** content has weightmap-layer *data assets*
    (`BiomeGenerators/Setup/PDA_LandscapeWeightmap_Layer`, `BiomeCore_WeightmapFilter`). These read and
    filter weights rather than paint them; this is inferred from asset names, since the assets are
    binary.
- **Experimental terrain writing exists, but for Mesh Partition (mesh terrain), not Landscape.**
  `Plugins/Experimental/PCGMeshPartitionInterop` (Experimental, disabled by default) adds:
  - **MeshPartition Write** (`Private/Elements/PCGMeshPartitionWrite.h`). It "edits vertices in the
    mesh partition by spawning a modifier" that moves vertices from source to destination positions
    taken from point attributes. It can also write float **weight channels** from point attributes.
    Settings: `AffectedMegaMesh`, modifier `Type="SimpleWrite"`, `Priority`, `Channels`,
    `SourcePositionsAttribute`, `DestinationPositionsAttribute`, `bConstrainToBounds`.
  - **MeshPartition Sculpt Layer Write**, the same idea through a Sculpt Layers modifier.
  - **Mesh Partition Patch Instance Spawner** and **Mesh Partition Projection Instance Spawner**, which
    spawn patch or projection modifiers and can write a weight channel.
  - Readers: Mesh Partition Query, Get Mesh Terrain Section, Get Mesh Terrain Section Channel Textures,
    Get Mesh Partition Grass Types, Get Mesh Partition Texel Sizes, Bake Mesh Terrain Section Mesh.
  - Terrain edits are **managed resources** too (`UPCGManagedPatchModifier : UPCGManagedComponent`),
    so they follow the same soft/hard release cycle as meshes. The design is
    **non-destructive and modifier-based**: PCG spawns modifier objects with a priority instead of
    baking into the base data.
- **Other world outputs that do exist:**
  - Spawned actors and components: Spawn Actor, Create Target Actor, Add Component, Spawn Spline Component.
  - Property writes: Apply On Object.
  - Destructive editor removal: Remove Actors From World.
  - Data Layer and HLOD assignment of spawned actors (`FPCGDataLayerSettings`, `FPCGHLODSettings`).
  - Asset writes: Save PCG Data Asset, Save Texture to Asset, and `PCGEditor/Public/PCGLevelToAsset.h`
    (level to PCG data asset).
  - Interop outputs: Spawn Dynamic Mesh, Save Dynamic Mesh To Asset (GeometryScript interop),
    Spawn Instanced Actors, Write To Niagara Data Channel.

---

## 7. Partitioning, hierarchical generation and runtime generation

- **Partitioning.** With `bIsComponentPartitioned`, the original component's bounds are split on a
  grid. `APCGWorldActor::PartitionGridSize` defaults to 25600 units, i.e. 256 m
  (`Public/PCGWorldActor.h:59-63`). Each cell gets an `APCGPartitionActor`
  (`Public/Grid/PCGPartitionActor.h`, `GetGridCoord`, `AddGraphInstance`) holding a **local component**
  that runs the same graph limited to the cell bounds. Its Input is cell volume ∩ original actor
  data (`PCGGetDataFunctionRegistry.cpp:175-210`). The tracking manager maps original ↔ local
  (`PCGTrackingManager.h`).
- **Hierarchical generation (HiGen).**
  - Enabled with `UPCGGraph::bUseHierarchicalGeneration`, default grid `HiGenGridSize = Grid256`.
  - Grid enum `EPCGHiGenGrid` runs from Grid4 (4 m) to Grid2048 (2048 m), plus `Unbounded`
    (`Public/PCGCommon.h:520-540`).
  - Set Grid Size / Change Grid Size nodes assign downstream nodes to grid levels. The compiler
    produces a task list per grid size (`GetCompiledTasks(..., GenerationGridSize)`).
  - Elements with `IsGridLinkage()` (`PCGElement.h:193`) pass data from a coarse grid to finer ones.
    Large features (e.g. tree placement) are computed once at a large cell, and small detail per
    small cell.
  - Sample: `Content/SampleContent/HiGenForest`, `GraphTemplates/TPL_Showcase_HierarchicalGenerationForest`.
- **Runtime generation.**
  - `GenerateAtRuntime` hands the component to `UPCGRuntimeGenScheduler`
    (`Public/RuntimeGen/PCGRuntimeGenScheduler.h`).
  - Generation sources: `PCGGenSourceComponent`, `PCGGenSourcePlayer`, `PCGGenSourceEditorCamera`,
    `PCGGenSourceWPStreamingSource` (`Public/RuntimeGen/GenSources/`).
  - Per-grid generate and cleanup radii come from `FPCGRuntimeGenerationRadii`, with default
    multipliers 2.0 and 1.1 (`PCGCommon.h:596-621`). Partition actors are pooled and transient.
- **Relevant to an editor-only bake:**
  - Partitioning and HiGen are mainly about scaling and streaming. An editor bake needs only
    `GenerateOnDemand`/`GenerateOnLoad`, Normal editing mode (so resources are saved), and optionally
    partitioning for very large areas.
  - `PCGEditor/Private/WorldPartitionBuilder/PCGWorldPartitionBuilder.*` with `PCGBuilderSettings.h` is
    a commandlet/dialog that loads the world and generates listed graphs offline in order, filtered by
    editing mode and actor name. This is Unreal's batch-bake path.

---

## 8. Electric Dreams-style workflow patterns visible on disk

The Electric Dreams sample project is **not** on this machine (no match under `F:\Engine`). Its
patterns do appear in shipped content, most completely in **PCGBiomeCore**
(`Plugins/Experimental/PCGBiomeCore/Content`). Evidence comes from readable names inside the binary
`.uasset` files:

- **Tagged control actors with spline components.**
  - `Blueprints/BP_PCGBiomeExclusionSpline` contains a `SplineComponent` with component tag
    `PCG_BiomeExclusion` ("Spline based biome exclusion").
  - `BP_PCGBiomeExclusionVolume`, `BP_PCGBiomeSpline` ("Biome Spline"), `BP_PCGBiomeVolume` and
    `BP_PCGBiomeTexture` carry tags such as `PCG_BiomeActorSpline`, `PCG_BiomeActorVolume` and
    `PCG_BiomeActorTextureVolume`.
  - The main graph `BiomeCore.uasset` contains `GetActorData` nodes using `ActorSelectionTag` and
    `ComponentSelectionTag`, `GetDataFromPCGComponent`, and the filter "Filters out actors that do not
    overlap the source component bounds". In other words: select control actors by tag, constrained to
    component bounds.
- **Exclusion and openings through Difference.**
  - `Core/BiomeCore_DifferenceByPriority` contains `Difference` nodes using `EPCGDifferenceDensityFunction`
    and `EPCGDifferenceMode`, plus `FilterDataByTag`/`GetTags`/`DeleteTags` and a `GeneratorPriority` /
    `BIOMEPRIO` tag scheme. The pin tooltip is quoted in the source: "Inputs on this pin will be implicitly
    unioned together to be used in the difference operation".
  - The pattern: exclusion shapes (spline interiors or volumes), unioned on the `Differences` pin,
    subtracted from sample points or areas.
- **Graph parameters.** The main graph contains `GetGraphParameter` nodes. Biome actors expose
  per-instance settings such as blend density or noise and a local biome definition. Generators,
  definitions and filters are data assets (`BiomeDefinitions/`, `BiomeGenerators/`, `Filters/`).
- **Recipe-style subgraph library.** `Core/BiomeCore_*` contains about 50 small subgraphs, e.g.
  `BiomeCore_SelfPruningGraph`, `BiomeCore_ProjectionLoop`, `BiomeCore_SpawningLoop`,
  `BiomeCore_TilePartitioner`. Engine-level reusable subgraphs are in `PCG/Content/Subgraphs`
  (`CutSplines`, `DiscardPointsOnIrregularSurface`, `ElevationIsolines*`, `SpatialDataBoundsToPoint`).
  Templates are in `PCG/Content/GraphTemplates` (`TPL_Sampler_Spline`, `TPL_Sampler_SplineInterior`,
  `TPL_Sampler_Volume`, `TPL_Showcase_SimpleForest`, `_Default_Loop`, ...).
- **Interactive authoring.** The PCG Editor Mode (`PCGEditor/Private/EditorMode/PCGEdMode*`) provides
  drawing tools. Their output reaches graphs through `UPCGToolData` / **Get Tool Data**
  (`Public/Data/PCGToolData.h`, `Public/Elements/PCGDataFromTool.h`). PCGBiomeCore ships
  `EdMode/BiomeSplineTool` and `BiomeVolumeTool`.

---

## 9. What maps to our engine (Unreal concepts, ordered by need)

This list states Unreal facts only, in the order a minimal-but-real data-driven system would need them.

1. **Graph asset**: nodes, typed pins and edges, Input and Output nodes (`UPCGGraph`, `UPCGNode`, `UPCGPin`).
2. **Node = settings + stateless element**: authored properties separate from execution code, one element per settings object (`UPCGSettings::CreateElement/GetElement`, `IPCGElement`).
3. **Typed data passed between nodes as tagged collections** (`FPCGDataCollection`, `FPCGTaggedData{Data, Tags, Pin}`).
4. **Point data** with transform, density, bounds, color, steepness, seed and a metadata entry (`FPCGPoint`, `UPCGPointArrayData`).
5. **Per-point attributes**, with parent-chained metadata and a uniform property/attribute selector (`UPCGMetadata`, `FPCGAttributePropertySelector`).
6. **Spatial data interface**: bounds, density at position, sample and project (`UPCGSpatialData`), with concrete types for spline, spline interior, landscape/surface, volume and primitive.
7. **Execution source** owning seed, bounds and input data, i.e. the component on an actor or volume (`UPCGComponent`, `APCGVolume`, Input node pins).
8. **Deterministic seeding**: `ComputeSeed(nodeSeed, componentSeed)` then `ComputeSeed(contextSeed, pointSeed)`; point seeds from cell index or position.
9. **World readers**: Get Actor Data by tag or class with bounds overlap, Get Spline Data, Get Landscape Data (height, normal, layer weights).
10. **Core node set for environments**: Surface Sampler, Spline Sampler (incl. `OnInterior`), Density Filter, Attribute Noise, Transform Points, Self Pruning, Difference, Projection, Bounds Modifier, Distance, Filter Data By Tag, Copy Points.
11. **Spawners with weighted selection**: Static Mesh Spawner + `UPCGMeshSelectorWeighted`, Spawn Actor.
12. **Managed resources**: soft release, then reuse, then sweep the unused on regenerate; hard release on Cleanup (`UPCGManagedResource` family, `CleanupUnusedManagedResources`).
13. **Generate / Cleanup API and trigger modes** (OnLoad / OnDemand / AtRuntime), plus an editor-only Preview mode that does not save results.
14. **Graph user parameters and per-instance overrides** (`UserParameters`, `UPCGGraphInstance::ParametersOverrides`, Get Graph Parameter).
15. **Subgraphs and loops** for reusable recipes (`UPCGSubgraphSettings`, `UPCGLoopSettings`).
16. **Change tracking** keyed by tag, class or actor, with bounds culling, landscape tracking and self-change suppression (`FPCGTrackingManager`, `FPCGSelectionKey`).
17. **CRC-keyed element cache**: key = element + CRC(input data CRCs, settings CRC, seed if used); LRU memory budget (`FPCGGraphCache`, `GetDependenciesCrc`).
18. **Terrain-modifying outputs** as managed, prioritized, non-destructive modifiers. In 5.8 these exist only for Mesh Partition, as Experimental (MeshPartition Write / Sculpt Layer Write / patch and projection spawners); there is none for classic Landscape.
19. **Scale-out features**: partition grid with local components, hierarchical grid levels, runtime scheduler, and an offline builder for batch bakes (`APCGPartitionActor`, HiGen, `UPCGRuntimeGenScheduler`, `PCGWorldPartitionBuilder`).

# Trees of Insanity

This context describes plant growth concepts used by the project, following the branch-module growth language from Synthetic Silviculture.

## Language

**Ecosystem**:
The simulated collection of plants and environmental conditions for a simulation run.
_Avoid_: Scene, USD stage, renderer world

**Simulation run**:
One evolution of an ecosystem from initial conditions under a fixed set of growth inputs.
_Avoid_: Session, playback, render run

**Project**:
A saved user workspace containing the selected branch module prototype, plant type library, and active plant type.
_Avoid_: Document, scene file, save file

**Module workspace**:
The product scope for inspecting a branch module prototype and previewing branch module instances grown from it.
_Avoid_: Module level, module view, module editor

**Plant workspace**:
The product scope for developing and inspecting one plant architecture made of branch modules.
_Avoid_: Plant level, plant view, plant editor

**Ecosystem workspace**:
The product scope for developing and inspecting ecosystems.
_Avoid_: Ecosystem level, ecosystem view, ecosystem editor

**Workspace preview**:
A small live preview of an inactive workspace that can be selected to make that workspace active.
_Avoid_: Tab, thumbnail, workspace button

**Plant**:
An individual simulated organism in an ecosystem, governed by one plant type.
_Avoid_: Tree, instance, actor

**Plant type**:
A user-owned parameter set that defines the growth behavior and architecture tendencies of modeled plants.
_Avoid_: Species

**Plant type preset**:
A built-in reference parameter set that can be copied into a plant type.
_Avoid_: Species template, active template

**Plant type draft**:
An editable in-progress plant type before it is saved as a plant type.
_Avoid_: Temporary species, unsaved template

**Plant type library**:
The user-owned collection of plant types available to workspaces.
_Avoid_: Species list, template library, preset library

**Plant type manager**:
The shared product scope for creating, deleting, and editing plant types in the plant type library.
_Avoid_: Species editor, template manager, preset editor

**Branch module prototype**:
A reusable template for branch module topology, geometry, and developmental attributes.
_Avoid_: Mesh asset, prefab

**Branch module prototype library**:
The collection of branch module prototypes available to the Module workspace.
_Avoid_: OBJ list, mesh library, prefab library

**Branch module**:
A self-similar branching structure represented by branch nodes and branch segments.
_Avoid_: Branch chunk, sub-tree, module piece

**Branch module instance**:
A simulated occurrence of a branch module prototype at a particular module physiological age.
_Avoid_: Preview tree, generated module, rendered module

**Prototype inspector**:
The module workspace pane for inspecting branch module prototype nodes, segments, and attributes.
_Avoid_: Node editor, data view, debug panel

**Growth preview**:
The module workspace pane for previewing a branch module instance grown from a branch module prototype.
_Avoid_: Render view, simulation view, output pane

**Growth preview guide**:
A viewport aid drawn over the Growth preview, such as world-origin axes or a future reference grid. It is not part of the branch module instance or render chain mesh data.
_Avoid_: Branch geometry, USD scene object, viewport overlay

**Render mode**:
The selectable way the Growth preview draws its content, such as the rendered mode used today or a future solid raster preview.
_Avoid_: Viewport shading, display mode, shading mode

**HDRI environment**:
The high-dynamic-range image that lights the Growth preview and can appear as its backdrop.
_Avoid_: HDRI background, skybox, background image

**HDRI environment library**:
The collection of bundled and user-added HDRI environments available to the Growth preview.
_Avoid_: HDRI library, backdrop library, image picker

**Application preferences**:
User-level choices that control the application experience across projects.
_Avoid_: Project settings, scene settings

**Growth snapshot**:
The computed visible state of a branch module instance at a chosen physiological age.
_Avoid_: Render data, scene data, frame

**Branch node**:
A point in a branch module prototype that can carry a node physiological age and connect branch segments.
_Avoid_: Vertex, point, joint

**Branch segment**:
A directed connection from a base node to a child node inside a branch module.
_Avoid_: Edge, line, curve piece

**Continuation segment**:
The child branch segment at a branch node that best preserves the incoming segment direction; render projection welds it to the incoming branch segment.
_Avoid_: Main child, primary branch

**Lateral segment**:
A child branch segment at a branch node that is not the continuation segment; render projection starts it as a separate chain from the shared centerline node.
_Avoid_: Side mesh, secondary branch

**Render chain**:
A render projection mesh made from one or more branch segments connected by continuation-segment relationships.
_Avoid_: Curve, branch chunk, mesh instance

**Prototype element**:
Either a branch node or a branch segment in a branch module prototype.
_Avoid_: Item, object, selection target

**Base node**:
The branch node where a branch segment starts; its node physiological age delays that segment's growth.
_Avoid_: Parent vertex, start point

**Child node**:
The branch node where a branch segment ends.
_Avoid_: End vertex, child point

**Terminal segment**:
A branch segment with no child segments.
_Avoid_: Leaf segment, tip edge

**Module physiological age**:
A scalar developmental state of a branch module; it is not calendar time, but it increases over simulation time according to the module's growth rate.
_Avoid_: Module time, module age, real age

**Module mature age**:
The physiological-age threshold at which a branch module is considered fully developed; the paper denotes this as `a_mature`.
_Avoid_: Plant maturity, flowering age

**Fully developed module**:
A branch module whose module physiological age has reached its module mature age and can serve as a parent for subsequent module attachment.
_Avoid_: Finished tree, mature plant

**Simulation time**:
The time variable advanced by the growth simulation; physiological ages change as simulation time advances.
_Avoid_: Age

**Simulation time step**:
The increment of simulation time used by one Euler integration update.
_Avoid_: Age step, frame

**Growth rate**:
The rate at which a branch module's physiological age changes over simulation time; the paper denotes this as `Υ(u)`.
_Avoid_: Speed, timestep

**Plant growth rate**:
A plant-type parameter that scales module growth rates; the paper denotes this as `g_p`.
_Avoid_: Simulation speed, frame rate

**Vigor**:
A scalar growth potential for a branch module, used by the paper to derive the module's growth rate.
_Avoid_: Energy, health, speed

**Module light exposure**:
A scalar estimate of available light or space for a branch module; the paper denotes this as `Q(u)` and derives it from intersections between module bounding volumes.
_Avoid_: Brightness, sunlight value

**Basipetal pass**:
A tip-to-root traversal that accumulates module light exposure through the plant architecture.
_Avoid_: Upward pass, backwards pass

**Acropetal pass**:
A root-to-tip traversal that distributes vigor through the plant architecture.
_Avoid_: Downward pass, forwards pass

**Terminal node**:
A branch module node that can serve as an attachment point for a child branch module once the parent module is fully developed.
_Avoid_: Bud point, endpoint

**Terminal vigor**:
A scalar vigor value distributed to a terminal node and used to decide whether a child branch module attaches there.
_Avoid_: Child spawn chance, terminal energy

**Node physiological age**:
A scalar developmental offset associated with a branch node.
_Avoid_: Node time, activation frame

**Segment physiological age**:
The local developmental state of a branch segment, derived from module physiological age and the node physiological age of the segment's base node.
_Avoid_: Segment time, visible age

**Segment length**:
The current grown length of a branch segment.
_Avoid_: Branch length, edge length

**Maximum segment length**:
The final length a branch segment can reach, normally authored by the branch module prototype geometry.
_Avoid_: Target branch length, rest length

**Segment diameter**:
The current thickness of a branch segment.
_Avoid_: Width, radius

**Segment state**:
The visible developmental state of a branch segment, such as growing or mature.
_Avoid_: Render state, curve state

**Tropism**:
A directional growth tendency that bends branch segment direction during development.
_Avoid_: Gravity, bend, wind

**Tropism strength**:
A plant-type parameter controlling how strongly tropism offsets branch segment direction.
_Avoid_: Bend strength, gravity amount

**Apical control**:
A plant-type parameter that biases growth allocation toward apical branches.
_Avoid_: Top dominance, branch priority

**Determinacy**:
A plant-type parameter that describes how strongly growth is bounded or tends toward termination.
_Avoid_: Determinism, fixedness

**Flowering age**:
A plant-type threshold at which mature plant-type parameters may begin to apply.
_Avoid_: Bloom time, maturity age

**Root max vigor**:
A plant-type parameter for the maximum vigor allocated to the root module.
_Avoid_: Root energy, starting health

**Module architecture**:
The ordered tree of connected branch modules that forms one plant, rooted at the root module.
_Avoid_: Plant graph, module tree, scene graph

**Root module**:
The base branch module of a plant's module architecture; it receives the plant's root max vigor.
_Avoid_: Trunk module, base module

**Morphospace**:
The 2D parameter space spanned by apical control and determinacy in which branch module prototypes are positioned; Voronoi regions over prototype positions select which prototype a new branch module instantiates.
_Avoid_: Prototype space, parameter grid

**Module orientation**:
The Euler-angle orientation chosen for a newly attached branch module by minimizing an orientation quality function over available space and tropism.
_Avoid_: Branch angle, rotation

**Shedding threshold**:
The minimum vigor a branch module must retain to stay in the module architecture; below it the module is shed. The paper denotes this as `v̄_min`.
_Avoid_: Cull threshold, death value

**Plant age**:
A per-plant developmental counter; the paper denotes this as `p_t`. On reaching plant max age it triggers plant senescence.
_Avoid_: Plant time, calendar age

**Plant senescence**:
The end-of-life phase (plant age ≥ plant max age) in which root max vigor interpolates to zero and all branch modules are gradually shed.
_Avoid_: Death, decay, wilting

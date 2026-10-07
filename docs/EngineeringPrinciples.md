# Engineering Principles

## Purpose and Use

These principles are engineering heuristics, not absolute constraints. They help contributors, implementation agents, reviewers, and maintainers decide whether a proposed design is merely possible or is the simplest correct model worth maintaining.

Different changes require attention to different principles. A small, local, mechanical change does not need a ceremonial analysis of every item in this document. A non-trivial change involving ownership, state, lifecycle, persistence, platform coupling, process or thread boundaries, or cross-module relationships should use the relevant principles to challenge the proposed model before it becomes expensive to change.

The project may deliberately accept another state, boundary, process, lifecycle, abstraction, or dependency when the product problem genuinely requires it. A principle may be overridden by a concrete product or technical constraint when the tradeoff is understood and justified. The purpose is to ensure that complexity is intentional and earns its cost, not to label every deviation an engineering error.

The central question is:

> Do not optimize for "can it be implemented?" Optimize for "is this the simplest correct model worth maintaining?"

## Core Principle

Software becomes difficult to maintain primarily through relationships: ownership relationships, synchronization relationships, dependency relationships, lifecycle ordering, duplicated state, and assumptions spread across boundaries. Lines of code matter, but they are often a weak proxy for the number of things that must remain correct together.

Prefer a model that removes unnecessary relationships over one that manages them with increasingly sophisticated machinery. A sound design makes important facts easy to locate, lifetimes easy to explain, invalid states difficult to enter, and changes local enough to reason about.

## Applying the Principles Without Overengineering

The principles themselves must not become justification for additional architecture. Applying them should not automatically produce a Manager, Coordinator, Controller, Registry, Adapter, Facade, state machine, framework, or extra abstraction layer.

The preferred result is often:

- fewer objects;
- fewer owners;
- fewer states and transitions;
- fewer synchronization relationships;
- fewer boundary crossings;
- a direct function call;
- an existing type with a clearer contract;
- or no new mechanism at all.

A new abstraction is justified only when it reduces what the rest of the system must know, own, synchronize, or coordinate. Moving complexity behind a name without reducing it is concealment, not simplification.

## Resolving Tension Between Principles

Principles sometimes pull in different directions. Use engineering judgment and evaluate the whole system rather than invoking one principle in isolation. A useful, non-binding priority order is:

1. Product correctness and frozen behavioral invariants
2. Data safety
3. Clear ownership and source of truth
4. Dependency direction and architectural boundaries
5. State-space reduction
6. Local reasoning
7. Reversibility and blast-radius control
8. Implementation convenience

Implementation convenience alone is weak justification for long-term ownership, lifecycle, or dependency complexity.

## Decision Questions

These questions are a design lens, not a form that must be completed for every change.

| Area | Questions |
|---|---|
| Actual requirement | What user or system outcome is required? Which behavior is frozen, and which details are implementation choices? |
| Mechanism necessity | Why does the proposed mechanism need to exist? Can the outcome be achieved by removing or reusing something instead? |
| Ownership | Who owns each important object and fact? Is any ownership shared, ambiguous, or implied? |
| Creation and destruction | Who creates it, who destroys it, and what must still be alive at each point? |
| Source of truth | Where is the authoritative value? Are other representations derived, cached, or competing? |
| Invariants | What must always be true before, during, and after the operation? |
| Coupling | Which components must know about one another, and is each relationship essential? |
| Boundary crossings | Which module, process, thread, platform, persistence, or UI boundaries are crossed? What contract carries the data? |
| Duplicated state | Is the same fact stored in more than one place? If so, why can one representation not be derived? |
| Temporal coupling | What must happen first, later, or exactly once? What breaks if calls are delayed, repeated, or reordered? |
| Local reasoning | Can a maintainer understand the behavior by reading a small, cohesive area of the code? |
| Invalid states | Which contradictory or partially initialized states can be represented? Can the type or ownership model rule them out? |
| Blast radius | How many modules, files, tests, persisted formats, and runtime paths must change? Is that proportionate to the feature? |
| Reversibility | Can the decision be replaced or removed without a broad migration? Which commitments are hard to undo? |
| Accidental complexity | Which complexity belongs to the problem, and which exists only because of the chosen implementation? |
| Cost and benefit | What ongoing value does each new object, state, abstraction, and boundary provide for its lifetime cost? |

## Engineering Principles

### 1. Complexity Is Relationships, Not Lines of Code

A short implementation can be architecturally expensive if it introduces another owner, cache, callback, lifecycle phase, or dependency. A longer direct implementation can be simpler when its state and control flow remain local.

Count the relationships that must stay correct together. Ask who must coordinate with whom, what must synchronize, and what ordering assumptions now exist. Optimize for reducing those obligations rather than minimizing the visual size of a diff.

### 2. One Fact Should Have One Owner and One Source of Truth

Every authoritative fact should have one clear owner. Other layers may observe, transform, project, or cache it, but they should not independently decide its value.

When two mutable representations claim authority, every mutation gains a synchronization problem and every failure path risks disagreement. Name which representation is canonical and make the others explicitly derived or transient.

### 3. Prefer Eliminating Synchronization Over Improving Synchronization

Synchronization code can be made more careful, but the most reliable synchronization relationship is one that does not exist. Before adding notifications, callbacks, revision counters, invalidation flags, or reconciliation logic, ask whether one copy of the state can be removed.

If a value can be computed cheaply from authoritative data, derive it. If an update can cross a boundary as an explicit input, avoid maintaining a second mutable mirror.

### 4. Prefer Eliminating Ownership Conflicts Over Managing Them More Carefully

Reference counting, ownership flags, shutdown protocols, and defensive nulling may reduce symptoms without answering who is responsible for an object. When two subsystems appear to own the same lifetime, first move ownership to the boundary that naturally controls creation and destruction.

Non-owning references should be explicit and shorter-lived than their owner. If ownership must be shared, the reason should come from the product model rather than convenience.

### 5. Define Invariants Before Mechanisms

State what must remain true before choosing classes, APIs, or event flows. Invariants such as "the old resource remains active until replacement succeeds" or "a bound identity never silently falls back" are more durable than a particular mechanism.

Once the invariants are clear, mechanisms can be compared by how directly they preserve them and how many new failure states they introduce.

### 6. Make Invalid States Difficult or Impossible to Represent

Prefer types and ownership structures that exclude contradictions. A single enum is safer than several booleans whose combinations include impossible modes. A validated value type is safer than passing an arbitrary string through every layer.

Not every invalid state can be removed, especially at I/O and platform boundaries. Validate those states once at the boundary, then keep the internal model narrower and more trustworthy.

### 7. A Small Feature Should Have a Small Architectural Footprint

The amount of architecture introduced should be proportional to the product problem. A local UI behavior should not create a global service. A single synchronous resource path should not create a generic asynchronous framework.

When a small feature touches many owners or modules, treat the mismatch as evidence. The current boundary may be wrong, the feature may be less local than expected, or the proposed abstraction may be amplifying the change.

### 8. Preserve Locality

Keep behavior near the state and lifetime it depends on. A maintainer should be able to understand a feature by reading a small, cohesive part of the system rather than reconstructing control flow across unrelated callbacks and global registries.

Locality applies to failure handling as well as success paths. Recovery decisions should be made where enough context exists to choose correctly, while lower layers should report failures without inventing product policy.

### 9. Preserve Dependency Direction

High-level policy may coordinate lower-level capabilities; lower-level mechanisms should not reach upward to discover policy. Rendering should not query runtime selection, persistence should not control UI workflow, and platform code should not learn product semantics merely to complete an operation.

Cross a dependency boundary through the smallest contract that communicates intent. Avoid including a higher layer only to obtain one value that could have been passed explicitly.

### 10. Boundary Crossings Are Expensive

Every module, process, thread, platform, persistence, or UI boundary introduces translation, failure, lifetime, and testing costs. The cost is justified when the boundary isolates real volatility or ownership, not simply because another layer appears tidy.

Keep boundary contracts narrow, semantic, and free of implementation-specific handles where possible. Cross once with complete information rather than repeatedly querying across the boundary.

### 11. Minimize Temporal Coupling

Temporal coupling exists when correctness depends on calls occurring in a particular order or within an implicit time window. Initialization-before-use is sometimes essential, but hidden sequences such as "refresh, then clear, then rebind before the next timer tick" are fragile.

Prefer operations that are complete transactions, explicit state transitions, or naturally idempotent. Where ordering is unavoidable, encode and test it rather than relying on convention.

### 12. Align Ownership and Lifecycle

The component that owns a resource should normally control the events that create, replace, and destroy it. File-decoding objects should own decoded CPU data; rendering objects should own GPU resources; UI parents should own their Qt children.

When ownership and lifecycle control are split, shutdown and replacement acquire extra coordination. Move the resource or move the lifecycle decision until the relationship is direct.

### 13. Store Causes; Derive Consequences

Persist or own the facts users author and the system cannot otherwise recover. Derive display labels, effective values, projections, layout geometry, and other consequences from those facts.

Storing both a cause and all of its consequences creates invalidation work and stale-state bugs. Cache a derived value only when evidence shows the computation or boundary cost matters, and make the cache's authority and invalidation contract explicit.

### 14. Prefer State-Space Reduction Over Coordination Sophistication

A design with fewer possible states is usually easier to verify than one with a powerful coordinator that handles every combination. Remove optional intermediate states, collapse mutually dependent flags, and make transitions atomic where practical.

Do not celebrate a state machine merely because it models complexity accurately. First ask whether the product requires all of those states.

### 15. Abstraction Should Reduce Complexity, Not Conceal It

An abstraction earns its place when callers need to know less, ownership becomes clearer, or variation is isolated. A class named Manager or Service does not help if callers still understand the same lifecycle and the implementation merely forwards calls.

Judge an abstraction by the relationships it removes. If it adds an interface, registry, factory, and synchronization rule without reducing knowledge elsewhere, it has increased complexity.

### 16. Reuse the Correct Abstraction, Not Merely Existing Machinery

Existing code is not automatically the right home for new behavior. Reusing a timer because one already exists, a cache because it can hold another type, or a renderer API because it can accept an opaque pointer may create accidental coupling.

Reuse when the existing abstraction owns the same concept and preserves its invariants. Otherwise prefer a small direct addition at the correct boundary over forcing a mismatched mechanism to become generic.

### 17. Prefer Architectural Simplicity Over Implementation Convenience

The easiest place to add a field or call is often the layer currently in hand, not the layer that should own it. Short-term convenience can leave a permanent dependency, duplicate state, or lifecycle exception.

Accept local implementation effort when it preserves a simpler ownership model. Conversely, do not perform a broad purity refactor when a direct local solution already respects the architecture.

### 18. Optimize for Reversibility

Prefer decisions that can be replaced independently: narrow contracts, isolated platform code, additive migrations, and explicit ownership boundaries. Avoid committing broad parts of the system to a mechanism before its uncertain assumptions have been tested.

For high-risk integration work, use phased slices and evidence gates. A reversible experiment may validate a boundary, but temporary risk-spike code should not silently become the production architecture.

### 19. Control Blast Radius

A change should affect only the modules whose responsibilities truly change. Broad diffs increase review cost, regression surface, merge conflict risk, and the chance of accidentally changing unrelated behavior.

Blast radius is also architectural. A public API, persisted schema, process protocol, or shared global can make future changes propagate far beyond the original feature. Keep such commitments narrow and deliberate.

### 20. Repeated Patches Are Evidence

When the same area requires repeated guards, ordering fixes, cleanup hooks, or special cases, stop treating each patch as isolated. The pattern may indicate a wrong ownership boundary, duplicated authority, or a missing invariant.

Repeated patches do not prove that a rewrite is needed. They justify a focused root-cause investigation and comparison with a simpler model.

### 21. Prefer Removing Causes Over Treating Symptoms

Retries, extra clearing, defensive checks, and synchronization can suppress visible failures while preserving the condition that creates them. Find the first point where state becomes invalid, ownership becomes ambiguous, or a contract is violated.

Fix the smallest proven cause. Do not replace one speculative patch with a broader speculative redesign.

### 22. Distinguish Essential Complexity From Accidental Complexity

Some complexity belongs to the product: decoding compressed media, recovering a graphics device, preserving user data, or crossing a process boundary may be inherently complex. Other complexity comes from duplicated state, misplaced policy, or premature generality.

Do not deny essential complexity. Contain it behind an appropriate boundary and keep unrelated layers free of it. Remove accidental complexity before adding machinery to manage it.

### 23. Complexity Must Earn Its Existence

Every long-lived abstraction, state, dependency, and lifecycle should provide identifiable value. The burden of proof rises with its blast radius and irreversibility.

Ask what present requirement the complexity satisfies, what simpler alternatives were rejected, and which ongoing obligations it removes. "We may need it later" is not enough by itself.

### 24. Prefer Designs Where Fewer Things Must Be Simultaneously Correct

Reliability improves when an operation depends on fewer owners, states, callbacks, and timing assumptions. A model that is locally complete is easier to test and recover than one whose correctness emerges from many components agreeing at once.

This principle summarizes the others: reduce the number of relationships that must hold together, and entire classes of failures become impossible rather than merely handled.

## Agent Behavior

For a non-trivial change, an implementation agent should generally:

1. Understand what the requirement actually means and identify frozen behavior.
2. Explain why any new machinery is necessary.
3. Identify ownership, state, invariants, lifecycle, and dependency direction.
4. Estimate synchronization, boundary-crossing, and blast-radius costs.
5. Compare at least one simpler model when the proposed complexity appears disproportionate.
6. Ask whether complexity can be removed instead of managed.
7. Choose the design with the lowest justified long-term coordination cost.
8. Only then implement and verify the relevant invariants.

Once a non-trivial mechanism is proposed, make a reasonable attempt to falsify or challenge it before committing to it. Trace failure paths, replacement and shutdown ordering, stale state, ownership conflicts, and simpler alternatives.

This depth is not required for every small edit. Apply it when a change introduces meaningful ownership, state, lifecycle, persistence, platform coupling, process or thread boundaries, or cross-module relationships.

## Review Use

These principles also serve as a review lens. The implementation agent may not identify every architectural concern before coding, so a reviewer should challenge the resulting behavior and model without turning review into ceremony.

Useful questions include:

- Is there duplicated state that could disappear?
- Is this synchronization obligation actually necessary?
- Why are there two owners for the same fact?
- Did a small feature gain an unexpectedly large architectural footprint?
- Does this lifecycle require too many ordering assumptions?
- Did a low-level layer learn about a high-level policy?
- Could this failure class disappear under a different ownership boundary?
- Are repeated patches evidence that the underlying model is wrong?
- Would another design require fewer things to remain simultaneously correct?

Review should distinguish concrete correctness or maintenance risk from stylistic preference. A familiar pattern is not inherently better than a direct solution, and a deliberate tradeoff is not a defect when its cost is understood.

## Compact Heuristics

- Start from the required behavior, not a preferred pattern.
- State invariants before naming mechanisms.
- Give every authoritative fact one owner.
- Store causes; derive consequences.
- Remove duplicate state before adding synchronization.
- Remove ownership ambiguity before adding lifetime guards.
- Prefer explicit calls and data flow over hidden coordination.
- Keep policy above mechanism and platform details behind platform boundaries.
- Treat every boundary crossing as a cost that needs a reason.
- Make failure and cleanup paths as explicit as success paths.
- Keep non-owning references shorter-lived than their owners.
- Prefer transactions that preserve the old valid state until the new state succeeds.
- Avoid states that exist only to bridge two competing models.
- Reuse an abstraction only when it owns the same concept.
- Judge abstractions by what callers no longer need to know.
- Do not create infrastructure for hypothetical variation.
- Let small features remain small.
- Keep changes local and rollback boundaries clear.
- Treat broad blast radius as evidence, not inevitability.
- Treat repeated patches as a prompt to revisit the model.
- Contain essential complexity; remove accidental complexity.
- Prefer one durable invariant over several ongoing reconciliation mechanisms.
- Challenge non-trivial mechanisms before committing to them.
- Choose the model where fewer things must be correct at the same time.

## Final Mental Model

Before committing to a non-trivial design, ask:

- What is the real problem?
- Why does this thing need to exist?
- Who owns it?
- Where is the truth?
- What must remain invariant?
- What must synchronize?
- What must happen in order?
- What boundaries are crossed?
- How far can this change propagate?
- What invalid states become possible?
- How hard is this to remove?
- What complexity are we buying?
- What value are we getting for it?
- Can we delete a relationship instead of managing it?
- Can we choose a model where fewer things need to be correct?

> **Continuously search for the boundary or model that makes unnecessary states, relationships, synchronization, ownership conflicts, and special cases disappear.**

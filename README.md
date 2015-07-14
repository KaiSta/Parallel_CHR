# Parallel_CHR
Constraint Handling Rules framework written in C++ that executes CHR concurrently and supports linear and non-linear rules.

By Kai Stadtmüller, Prof. Dr. Martin Sulzmann and Edmund S. L. Lam

Multi-Set replacement rules, such as Constraint Handling Rules, are a expressive formalism for the specification of concurrent problems like cryptographic protocols, multi-agent systems, scheduling etc. The biggest advantage is the
declarative specifications notation for which a direct implementation of requirements is often possible. Common implementations of Multi-set replacement rules operate sequentially. Therefore one rule after the other is processed which is not very efficient. Rules can be executed in parallel as long as they do not
conflict with each other. Three different approaches were therefore tested.

Pessimistic
===========
Only constraints that have the state pending are considered and found constraints that match the current rule are immediately locked by setting their state to claimed. 

Optimistic
==========
Pending and claimed constraints are considered during the search. When the match is complete, the found constraints are locked and the rule is triggered if the locking succeeds. Otherwise either a complete restart is executed,
in which everything is set back to start and all constraints need to be searched again or, as an alternative strategy, a partial restart is done. For the partial restart only the failed constraint is replaced by another one.


Requirements
============
* C++ 11 compiler
* Intel Thread Building Blocks library
* Mostly tested for windows with 64 bit


# Raft-Implementation

Raft is a consensus algorithm that can be used to manage a replicated log. Consensus algorithms allow a collection of machines to work as a coherent group even in instances of failures of some members. This resiliency is important in building reliable large-scale software systems.

This Raft implementation utilizes nodes that can take the following types:
- Follower : Nodes are initialized as followers. If a follower’s election timeout occurs because the leader becomes unresponsive or there is no leader, that follower will transition to being a candidate.
- Candidate : Candidates will request votes from the other nodes. If a majority of nodes assent to the candidacy, the candidate will become the leader. Otherwise, it will revert to being a follower.
- Leader : Leader nodes transmit messages from the proxy to follower nodes. If there are no messages, it will transmit heartbeats to followers to maintain its status as leader.

Each node regardless of type will maintain a state with a log of messages, index, term, and other key information.
The general flow of Raft is that nodes will be initialized as followers. If a follower’s election timeout occurs and that follower will transition to being a candidate. This triggers a leader election. The candidate will vote for itself and request the vote of other nodes. If the candidate receives a majority of votes, it will become a leader. The leader transmits append messages and heartbeats to followers.

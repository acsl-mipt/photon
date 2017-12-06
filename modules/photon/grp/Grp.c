#include "photon/grp/Grp.Component.h"
#include "photon/core/Logging.h"

#define _PHOTON_FNAME "grp/Grp.c"

#ifdef PHOTON_STUB

bool isSameGroup(uint64_t group)
{
    return (_photonGrp.group.type != PhotonOptionGrpGrpIdType_None && _photonGrp.group.data.someOptionGrpGrpId._1 == group);
}

void clearState()
{
    _photonGrp.commit = 0;
    _photonGrp.lastLogIdx = 0;
    _photonGrp.lastLogTerm = 0;
    _photonGrp.term = 0;
    _photonGrp.members.size = 0;
    _photonGrp.group.type = PhotonOptionGrpGrpIdType_None;
    _photonGrp.leader.type = PhotonOptionGrpUavIdType_None;
    _photonGrp.state = PhotonGrpMemberState_Follower;
    _photonGrp.votedFor.type = PhotonOptionGrpUavIdType_None;
    _photonGrp.timeouts.ping = 0;
    _photonGrp.timeouts.election = 0;
    _photonGrp.timeouts.election_rand = 0;
    _photonGrp.timeout_elapsed = 0;
}

void PhotonGrp_Init()
{
    clearState();
}

void PhotonGrp_Tick()
{
}

PhotonError PhotonGrp_SetTimeouts(uint64_t group, uint64_t ping, uint64_t lost)
{
    if (!isSameGroup(group))
        return PhotonError_InvalidDeviceId;

    _photonGrp.timeouts.ping = ping;
    _photonGrp.timeouts.election = ping*lost;
    PHOTON_INFO("SetTimeouts: group(%" PRIu64 "), ping(%" PRIu64 "), lost(" PRIu64 ")", group, ping, lost);
    return PhotonError_Ok;
}

PhotonError PhotonGrp_CreateGroup(uint64_t group, PhotonDynArrayOfGrpUavIdMaxSize10 const* members)
{
    if (!isSameGroup(group))
        return PhotonError_InvalidDeviceId;
 
    clearState();
    _photonGrp.group.type = PhotonOptionGrpGrpIdType_Some;
    _photonGrp.group.data.someOptionGrpGrpId._1 = group;
    _photonGrp.members.size = members->size;

    PHOTON_INFO("CreateGroup: group(%" PRIu64 "), count(%" PRIu64 ")", group, _photonGrp.members.size);
    for(uint64_t i = 0; i < _photonGrp.members.size; ++i)
    {
        _photonGrp.members.data[i] = members->data[i];
        PHOTON_INFO("CreateGroup: group(%" PRIu64 "), index(%" PRIu64 "), member(%" PRIu64 ")", group, i, _photonGrp.members.data[i]);
    }
    return PhotonError_Ok;
}

PhotonError PhotonGrp_DeleteGroup(uint64_t group)
{
    if (!isSameGroup(group))
        return PhotonError_InvalidDeviceId;

    PHOTON_INFO("DeleteGroup: group(%" PRIu64 ")", group);
    clearState();
    return PhotonError_Ok;
}

PhotonError PhotonGrp_AddMember(uint64_t group, uint64_t member, PhotonGrpReqCfgRep* rv)
{
    if (!isSameGroup(group))
        return PhotonError_InvalidDeviceId;

    if (_photonGrp.members.size >= sizeof(_photonGrp.members.data)/sizeof(_photonGrp.members.data[0]))
        return PhotonError_InvalidSize;
    _photonGrp.members.data[_photonGrp.members.size] = member;
    _photonGrp.members.size++;
    PHOTON_INFO("AddMember: group(%" PRIu64 "), member(%" PRIu64 ")", group, member);
    return PhotonError_Ok;
}

PhotonError PhotonGrp_RemoveMember(uint64_t group, uint64_t member, PhotonGrpReqCfgRep* rv)
{
    if (!isSameGroup(group))
        return PhotonError_InvalidDeviceId;

    if (_photonGrp.members.size == 0)
        return PhotonError_InvalidSize;
    _photonGrp.members.size--;
    PHOTON_INFO("RemoveMember: group(%" PRIu64 "), member(%" PRIu64 ")", group, member);
    return PhotonError_Ok;
}

PhotonError PhotonGrp_ReqVote(uint64_t group, uint64_t term, uint64_t lastLogIdx, uint64_t lastLogTerm)
{
    if (!isSameGroup(group))
        return PhotonError_InvalidDeviceId;

    PHOTON_INFO("ReqVote: group(%" PRIu64 "), term(%" PRIu64 "), lastLogIdx(%" PRIu64 "), lastLogTerm(%" PRIu64 ")", group, term, lastLogIdx, lastLogTerm);
    return PhotonError_Ok;
}

PhotonError PhotonGrp_ReqAppendEntry(uint64_t group, uint64_t term, uint64_t prevLogIdx, uint64_t prevLogTerm, uint64_t leaderCommit, PhotonDynArrayOfGrpLogEntryMaxSize10 const* entries)
{
    if (!isSameGroup(group))
        return PhotonError_InvalidDeviceId;

    PHOTON_INFO("ReqAppendEntry: group(%" PRIu64 "), term(%" PRIu64 "), prevLogIdx(%" PRIu64 "), prevLogTerm(%" PRIu64 "), leaderCommit(%" PRIu64 ")", group, term, prevLogIdx, prevLogTerm, leaderCommit);
    return PhotonError_Ok;
}


PhotonError PhotonGrp_ReqVoteRep(uint64_t group, uint64_t term, bool vote)
{
    if (!isSameGroup(group))
        return PhotonError_InvalidDeviceId;

    PHOTON_INFO("ReqVoteRep: group(%" PRIu64 "), term(%" PRIu64 "), lastLogIdx(%" PRIu64 "), lastLogTerm(%" PRIu64 ")", group, term, vote);
    return PhotonError_Ok;
}

PhotonError PhotonGrp_ReqAppendEntryRep(uint64_t group, uint64_t term, bool success, uint64_t currentIdx, uint64_t firstIdx)
{
    if (!isSameGroup(group))
        return PhotonError_InvalidDeviceId;

    PHOTON_INFO("ReqAppendEntryRep: group(%" PRIu64 "), term(%" PRIu64 "), success(%" PRIu64 "), currentIdx(%" PRIu64 "), firstIdx(%" PRIu64 ")", group, term, success, currentIdx, firstIdx);
    return PhotonError_Ok;
}

#endif

#undef _PHOTON_FNAME

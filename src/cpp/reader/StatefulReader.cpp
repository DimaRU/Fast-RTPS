/*************************************************************************
 * Copyright (c) 2014 eProsima. All rights reserved.
 *
 * This copy of eProsima RTPS is licensed to you under the terms described in the
 * EPROSIMARTPS_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/**
 * @file StatefulReader.cpp
 *
 */

#include "eprosimartps/reader/StatefulReader.h"
#include "eprosimartps/utils/RTPSLog.h"

#include "eprosimartps/dds/SampleInfo.h"
#include "eprosimartps/dds/DDSTopicDataType.h"

#include "eprosimartps/reader/WriterProxyData.h"

using namespace eprosima::dds;

namespace eprosima {
namespace rtps {

static const char* const CLASS_NAME = "StatefulReader";

StatefulReader::~StatefulReader()
{
	const char* const METHOD_NAME = "~StatefulReader";
	logInfo(RTPS_READER,"StatefulReader destructor.";);
	for(std::vector<WriterProxy*>::iterator it = matched_writers.begin();
			it!=matched_writers.end();++it)
	{
		delete(*it);
	}
}



StatefulReader::StatefulReader(const SubscriberAttributes& param,
		const GuidPrefix_t&guidP, const EntityId_t& entId,DDSTopicDataType* ptype):
																RTPSReader(guidP,entId,param.topic,ptype,STATEFUL,
																		param.userDefinedId,param.payloadMaxSize),
																		m_SubTimes(param.times)
{
	//locator lists:
	unicastLocatorList = param.unicastLocatorList;
	multicastLocatorList = param.multicastLocatorList;
	m_expectsInlineQos = param.expectsInlineQos;
}


bool StatefulReader::matched_writer_add(WriterProxyData* wdata)
{
	const char* const METHOD_NAME = "matched_writer_add";
	boost::lock_guard<Endpoint> guard(*this);
	for(std::vector<WriterProxy*>::iterator it=matched_writers.begin();
			it!=matched_writers.end();++it)
	{
		if((*it)->m_data->m_guid == wdata->m_guid)
		{
			logWarning(RTPS_READER,"Attempting to add existing writer");
			return false;
		}
	}
	WriterProxy* wp = new WriterProxy(wdata,m_SubTimes.heartbeatResponseDelay,this);
	matched_writers.push_back(wp);
	logInfo(RTPS_READER,"Writer Proxy " <<wp->m_data->m_guid <<" added to :" <<wp->m_data->m_guid);
	return true;
}
bool StatefulReader::matched_writer_remove(WriterProxyData* wdata)
{
	const char* const METHOD_NAME = "matched_writer_remove";
	boost::lock_guard<Endpoint> guard(*this);
	for(std::vector<WriterProxy*>::iterator it=matched_writers.begin();it!=matched_writers.end();++it)
	{
		if((*it)->m_data->m_guid == wdata->m_guid)
		{
			logInfo(RTPS_READER,"Writer Proxy removed: " <<(*it)->m_data->m_guid);
			delete(*it);
			matched_writers.erase(it);
			return true;
		}
	}
	logInfo(RTPS_READER,"Writer Proxy " << wdata->m_guid << " doesn't exist in reader "<<this->getGuid().entityId);
	return false;
}

bool StatefulReader::matched_writer_is_matched(WriterProxyData* wdata)
{
	boost::lock_guard<Endpoint> guard(*this);
	for(std::vector<WriterProxy*>::iterator it=matched_writers.begin();it!=matched_writers.end();++it)
	{
		if((*it)->m_data->m_guid == wdata->m_guid)
		{
			return true;
		}
	}
	return false;
}





bool StatefulReader::matched_writer_lookup(GUID_t& writerGUID,WriterProxy** WP)
{
	const char* const METHOD_NAME = "matched_writer_lookup";
	boost::lock_guard<Endpoint> guard(*this);
	for(std::vector<WriterProxy*>::iterator it=matched_writers.begin();it!=matched_writers.end();++it)
	{
		if((*it)->m_data->m_guid == writerGUID)
		{
			*WP = *it;
			logInfo(RTPS_READER,this->getGuid().entityId<<" FINDS writerProxy "<< writerGUID<<" from "<< matched_writers.size());
			return true;
		}
	}
	logInfo(RTPS_READER,this->getGuid().entityId<<" NOT FINDS writerProxy "<< writerGUID<<" from "<< matched_writers.size());
	return false;
}

bool StatefulReader::takeNextCacheChange(void* data,SampleInfo_t* info)
{
	const char* const METHOD_NAME = "takeNextCacheChange";
	boost::lock_guard<Endpoint> guard(*this);
	CacheChange_t* min_change;
	SequenceNumber_t minSeqNum = c_SequenceNumber_Unknown;
	SequenceNumber_t auxSeqNum;
	WriterProxy* wp;
	bool available = false;
	for(std::vector<WriterProxy*>::iterator it = this->matched_writers.begin();it!=matched_writers.end();++it)
	{
		(*it)->available_changes_min(&auxSeqNum);
		if(auxSeqNum.to64long() > 0 && (minSeqNum > auxSeqNum || minSeqNum == c_SequenceNumber_Unknown))
		{
			available = true;
			minSeqNum = auxSeqNum;
			wp = *it;
		}
	}
	if(available && wp->get_change(minSeqNum,&min_change))
	{
		logInfo(RTPS_READER,this->getGuid().entityId<<": trying takeNextCacheChange: "<< min_change->sequenceNumber.to64long());
		if(min_change->kind == ALIVE)
			this->mp_type->deserialize(&min_change->serializedPayload,data);
		if(wp->removeChangesFromWriterUpTo(min_change->sequenceNumber))
		{
			if(info!=NULL)
			{
				info->sampleKind = min_change->kind;
				info->writerGUID = min_change->writerGUID;
				info->sourceTimestamp = min_change->sourceTimestamp;
				if(this->m_qos.m_ownership.kind == EXCLUSIVE_OWNERSHIP_QOS)
					info->ownershipStrength = wp->m_data->m_qos.m_ownershipStrength.value;
				if(!min_change->isRead)
					m_reader_cache.decreaseUnreadCount();
				info->iHandle = min_change->instanceHandle;
			}
			if(!m_reader_cache.remove_change(min_change))
				logWarning(RTPS_READER,"Problem removing change " << min_change->sequenceNumber <<" from ReaderHistory");
			return true;
		}
	}
	return false;
}

bool StatefulReader::readNextCacheChange(void*data,SampleInfo_t* info)
{
	const char* const METHOD_NAME = "readNextCacheChange";
	boost::lock_guard<Endpoint> guard(*this);
	std::vector<CacheChange_t*> toremove;
	bool readok = false;
	for(std::vector<CacheChange_t*>::iterator it = m_reader_cache.changesBegin();
			it!=m_reader_cache.changesEnd();++it)
	{
		if((*it)->isRead)
			continue;
		WriterProxy* wp;
		if(this->matched_writer_lookup((*it)->writerGUID,&wp))
		{
			SequenceNumber_t seq;
			wp->available_changes_max(&seq);
			if(seq >= (*it)->sequenceNumber)
			{
				if((*it)->kind == ALIVE)
				{
					this->mp_type->deserialize(&(*it)->serializedPayload,data);
				}
				(*it)->isRead = true;
				if(info!=NULL)
				{
					info->sampleKind = (*it)->kind;
					info->writerGUID = (*it)->writerGUID;
					info->sourceTimestamp = (*it)->sourceTimestamp;
					info->iHandle = (*it)->instanceHandle;
					if(this->m_qos.m_ownership.kind == EXCLUSIVE_OWNERSHIP_QOS)
						info->ownershipStrength = wp->m_data->m_qos.m_ownershipStrength.value;
				}
				m_reader_cache.decreaseUnreadCount();
				logInfo(RTPS_READER,this->getGuid().entityId<<": reading change "<< (*it)->sequenceNumber.to64long());
				readok = true;
				break;
			}
		}
		else
		{
			toremove.push_back((*it));
		}
	}

	for(std::vector<CacheChange_t*>::iterator it = toremove.begin();
			it!=toremove.end();++it)
	{
		logWarning(RTPS_READER,"Removing change "<<(*it)->sequenceNumber.to64long()<< " from " << (*it)->writerGUID << " because is no longer paired";)
		m_reader_cache.remove_change(*it);
	}
	return readok;
}


bool StatefulReader::readNextCacheChange(CacheChange_t** change)
{
	const char* const METHOD_NAME = "readNextCacheChange";
	boost::lock_guard<Endpoint> guard(*this);
	std::vector<CacheChange_t*> toremove;
	bool readok = false;
	for(std::vector<CacheChange_t*>::iterator it = m_reader_cache.changesBegin();
			it!=m_reader_cache.changesEnd();++it)
	{
		if((*it)->isRead)
			continue;
		WriterProxy* wp;
		if(this->matched_writer_lookup((*it)->writerGUID,&wp))
		{
			SequenceNumber_t seq;
			wp->available_changes_max(&seq);
			if(seq >= (*it)->sequenceNumber)
			{
				*change = (*it);
				(*it)->isRead = true;
				m_reader_cache.decreaseUnreadCount();
				logInfo(RTPS_READER,this->getGuid().entityId<<": reads change "<< (*it)->sequenceNumber.to64long());
				readok = true;
			}
		}
		else
		{
			toremove.push_back((*it));

		}
	}
	for(std::vector<CacheChange_t*>::iterator it = toremove.begin();
			it!=toremove.end();++it)
	{
		logWarning(RTPS_READER,"Removing change "<<(*it)->sequenceNumber.to64long()<< " from " << (*it)->writerGUID << " because is no longer paired";)
		m_reader_cache.remove_change(*it);
	}
	return readok;
}


bool StatefulReader::isUnreadCacheChange()
{
	return m_reader_cache.isUnreadCache();
}

bool StatefulReader::change_removed_by_history(CacheChange_t* a_change,WriterProxy* wp)
{
	const char* const METHOD_NAME = "change_removed_by_history";
	boost::lock_guard<Endpoint> guard(*this);
	if(wp!=NULL || matched_writer_lookup(a_change->writerGUID,&wp))
	{
		std::vector<int> to_remove;
		for(size_t i = 0;i<wp->m_changesFromW.size();++i)
		{
			if(!wp->m_changesFromW.at(i).isValid() && (wp->m_changesFromW.at(i).status == RECEIVED || wp->m_changesFromW.at(i).status == LOST))
			{
				wp->m_lastRemovedSeqNum = wp->m_changesFromW.at(i).seqNum;
				to_remove.push_back((int)i);
			}
			if(a_change->sequenceNumber == wp->m_changesFromW.at(i).seqNum)
			{
				wp->m_changesFromW.at(i).notValid();
				break;
			}
		}
		for(std::vector<int>::reverse_iterator it = to_remove.rbegin();
				it!=to_remove.rend();++it)
		{
			wp->m_changesFromW.erase(wp->m_changesFromW.begin()+*it);
		}
		return m_reader_cache.remove_change(a_change);
	}
	else
	{
		logError(RTPS_READER," You should always find the WP associated with a change, something is very wrong");
	}
	return false;
}

bool StatefulReader::acceptMsgFrom(GUID_t& writerId,WriterProxy** wp)
{
	if(this->m_acceptMessagesFromUnkownWriters)
	{
		for(std::vector<WriterProxy*>::iterator it = this->matched_writers.begin();
				it!=matched_writers.end();++it)
		{
			if((*it)->m_data->m_guid == writerId)
			{
				if(wp!=NULL)
					*wp = *it;
				return true;
			}
		}
	}
	else
	{
		if(writerId.entityId == this->m_trustedWriterEntityId)
			return true;
	}
	return false;
}

bool StatefulReader::updateTimes(SubscriberTimes ti)
{
	if(m_SubTimes.heartbeatResponseDelay != ti.heartbeatResponseDelay)
	{
		m_SubTimes = ti;
		for(std::vector<WriterProxy*>::iterator wit = this->matched_writers.begin();
				wit!=this->matched_writers.end();++wit)
		{
			(*wit)->m_heartbeatResponse.update_interval(m_SubTimes.heartbeatResponseDelay);
		}
	}
	return true;
}

bool StatefulReader::add_change(CacheChange_t* a_change,WriterProxy* prox)
{
	//First look for WriterProxy in case is not provided
	if(prox == NULL)
	{
		if(!this->matched_writer_lookup(a_change->writerGUID,&prox))
		{
			return false;
		}
	}
	//WITH THE WRITERPROXY FOUND:
	//Check if we can add it
	if(a_change->sequenceNumber <= prox->m_lastRemovedSeqNum)
		return false;
	SequenceNumber_t maxSeq;
	prox->available_changes_max(&maxSeq);
	if(a_change->sequenceNumber <= maxSeq)
		return false;
	if(this->m_reader_cache.add_change(a_change,prox))
	{
		if(prox->received_change_set(a_change))
			return true;
	}
	return false;
}



} /* namespace rtps */
} /* namespace eprosima */

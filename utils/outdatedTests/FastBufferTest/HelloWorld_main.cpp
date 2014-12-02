/*************************************************************************
 * Copyright (c) 2014 eProsima. All rights reserved.
 *
 * This copy of eProsima RTPS is licensed to you under the terms described in the
 * fastrtps_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/**
 * @file HelloWorld_main.cpp
 *
 */

#include "fastrtps/rtps_all.h"
#include "HelloWorldPublisher.h"
#include "HelloWorldSubscriber.h"
#include "HelloWorldType.h"


int main(int argc, char** argv)
{
	RTPSLog::setVerbosity(EPROSIMA_DEBUGINFO_VERB_LEVEL);
	cout << "Starting "<< endl;
	int type = 1;
	if(argc > 1)
	{
		if(strcmp(argv[1],"publisher")==0)
			type = 1;
		else if(strcmp(argv[1],"subscriber")==0)
			type = 2;
	}
	else
	{
		cout << "publisher OR subscriber argument needed"<<endl;
		return 0;
	}

	HelloWorldType myType;
	DomainRTPSParticipant::registerType((DDSTopicDataType*)&myType);

	switch(type)
	{
	case 1:
	{
		HelloWorldPublisher mypub;
		for(int i = 0;i<10;++i)
		{
			if(mypub.publish())
			{
//				int aux;
//				std::cin>>aux;
				eClock::my_sleep(500);
			}
			else
			{
				//cout << "Sleeping till discovery"<<endl;
				eClock::my_sleep(100);
				--i;
			}
		}
		break;
	}
	case 2:
	{
		HelloWorldSubscriber mysub;
		cout << "Enter number to stop: ";
		int aux;
		std::cin>>aux;
		break;
	}
	}

	DomainRTPSParticipant::stopAll();

	return 0;
}
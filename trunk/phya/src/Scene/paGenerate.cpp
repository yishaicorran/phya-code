//
// paGenerate.cpp
//
// Global packaging for convenient multi-threaded output.
// Can work at a lower level if required.
//

#include "System/paConfig.h"

#if USING_AIO
	#include "AIO.hpp"
#endif


#include "Signal/paBlock.hpp"
#include "Signal/paLimiter.hpp"
#include "System/paThread.hpp"
#include "System/paCriticalSection.hpp"
#include "Scene/paTick.hpp"
#include "Scene/paFlags.hpp"
#include "Scene/paSceneAPI.h"


static void (*_monoCallback)(paFloat*);
static paLimiter *_limiter = 0;
static paFloat _limiterAttack = 0.0f;
static paFloat _limiterHold = 0.0f;
static paFloat _limiterRelease = 0.0f;


#if USING_AIO

static paThreadHandle _audioThreadHandle;
static paCriticalSection _critSec;				// Behaves like a mutex handle.

namespace paScene
{
	extern CAIOoutStream* outputStream;
}

static
PHYA_THREAD_RETURN_TYPE
_audioThread(void*)
{
	paBlock* output = new paBlock;

	while(paScene::audioThreadIsOn)
	{
		paEnterCriticalSection(&_critSec);			// Don't use paLock() because the locked flag is just for use by main thread.
			output = paTick();
		paLeaveCriticalSection(&_critSec);

		if (_limiter) _limiter->tick(output);

		// Allow the user to pass audio generated by paTick() to their own output stream.
		if (_monoCallback) _monoCallback(output->getStart());

		if (paScene::outputStreamIsOpen)
			paScene::outputStream->writeSamples(output->getStart(), paBlock::nFrames);
	}

//	printf("thread stops\n");
	return 0;
}


//// Single threaded operation.


PHYA_API
paFloat*
paGenerate()
{
	paBlock* output = paTick();

	if (paScene::outputStreamIsOpen)
	{
		if (_limiter) _limiter->tick(output);
		paScene::outputStream->writeSamples(output->getStart(), paBlock::nFrames);
	}

	// Allow the user to pass audio generated by paTick() to their own output stream.
	if (_monoCallback) _monoCallback(output->getStart());

	return output->getStart();
}






//// Double threaded operation

// Lock shared audio resource to current thread.
// If paLock() already in action in another thread, will block until first paLock() is released.
// Used to ensure only one thread works on shared audio memory at a time.

PHYA_API
void paLock() {
	if (!paScene::audioThreadIsOn) return;
	paAssert(("Already locked.", paScene::isLocked == false));  // Happens if you call paLock twice in same thread.
	paEnterCriticalSection(&_critSec);
	paScene::isLocked = true;
}

PHYA_API
void paUnlock() {
	if (!paScene::audioThreadIsOn) return;
	paAssert(("Not locked.", paScene::isLocked == true));
	paLeaveCriticalSection(&_critSec);
	paScene::isLocked = false;
}



PHYA_API
int
paStartThread()
{
	paInitializeCriticalSection(&_critSec);

	paScene::audioThreadIsOn = true;	// Have this read for the thread.

	_audioThreadHandle = paStartThread(_audioThread);

	if (paThreadErr(_audioThreadHandle))  {
		//error - couldn't start thread.
		paScene::audioThreadIsOn = false;
		return -1;
	}


	return 0;
}

PHYA_API
int
paStopThread()
{
	paStopThread(_audioThreadHandle);

	paScene::audioThreadIsOn = false;

	return 0;
}

//// Functions for non-blocking single thread operation, if you really wanted that kind of thing.


PHYA_API
int
paAutoGenerate()
{
	if ( paScene::outputStream->calcnDeviceBufferSamplesToFill() == -1 ) return -1;		// Device buffer not ready for writing.

	// Calculate audioblocks until there are enough to fill the device buffer
	// upto the amount worked out internally in the AIO library.

	while( paScene::outputStream->writeSamplesWithoutBlocking(paTick()->getStart(), paBlock::nFrames) != -1 );

	return 0;
}


PHYA_API
int
paAdaptiveAutoGenerate()
{
	if ( paScene::outputStream->calcnDeviceBufferSamplesToFillAdaptively() == -1 ) return -1;		// Device buffer not ready for writing.

	// Calculate audioblocks until there are enough to fill the device buffer
	// upto the amount worked out internally in the AIO library.

	while( paScene::outputStream->writeSamplesWithoutBlocking(paTick()->getStart(), paBlock::nFrames) != -1 );

	return 0;
}



#endif // USING_AIO

// Pass limiter time parameters. Zeros remove limiter.

PHYA_API
int
paSetLimiter(paFloat attackTime, paFloat holdTime, paFloat releaseTime)
{
	_limiterAttack = attackTime;
	_limiterHold = holdTime;
	_limiterRelease = releaseTime;
	return 0;
}



PHYA_API
int
paInit()
{
	if (_limiterAttack > 0)
		_limiter = new paLimiter(_limiterAttack,_limiterHold,_limiterRelease);
	return paTickInit();
}

PHYA_API
int
paSetOutputCallback( void (*cb)(paFloat*) )
{
	if (cb == 0) return -1;

	_monoCallback = cb;
	return 0;
}

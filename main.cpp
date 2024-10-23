#include "pooler.h"

#include <cstdio>
#include <unistd.h>

struct gooberData {
	int x;
	float b;
	double f;
};

POOLER_FUNC(myFunc, {
	gooberData* myDat = static_cast<gooberData*>(data);

	for (int i=0;i<2;i++) {
		printf("Hello from thread %d - x=%d, b=%f, f=%f\n", id, myDat->x, myDat->b, myDat->f);
		sleep(1);
	}
})

int main() {
	Pooler pool(2);

	gooberData inputData;
	inputData.x = 3532;
	inputData.b = 45.432;
	inputData.f = 4384737.384723;

	printf("Blocking until all threads complete their work...\n");
	pool.run(myFunc, static_cast<void*>(&inputData)); // inputData must not be modified after this point

	printf("Done!\nSending all threads a stop command...\n");
	pool.stop();
	
	printf("Done!\n");
	
	return 0;
}

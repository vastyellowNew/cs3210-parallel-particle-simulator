#include <mpi.h>
#include <iostream>
#include <cmath>
#include <vector> 
#include <algorithm>
#include <chrono>
#include <random>
#include <atomic>
#include <stdio.h>

using namespace std;

mt19937 rng;
random_device rd;
int n, l, r, s;
int paddedN;
int blockSize;
int mpiRank, size;

#define MASTER_ID 0

class Particle
{ 
	public: 
		operator string() const { 
			char buffer [200];
			snprintf(buffer, 200, "%d %.8lf %.8lf %.8lf %.8lf", i, x, y, vX, vY); 
			return buffer;
		}

		int i;
		double x;
		double y;
		double vX;
		double vY; 

		int pColl;
		int wColl;

		Particle() {};

		Particle(int i, double x, double y, double vX, double vY) 
		{
			this -> i = i;
			this -> x = x;
			this -> y = y;
			this -> vX = vX;
			this -> vY = vY;
			this -> pColl = 0;
			this -> wColl = 0;
		}

		int getIndex()
		{
			return this->i;
		}
		
		string getFullRepresentation()
		{
			
			char buffer[200];
			snprintf(buffer, 200, "%d %.8lf %.8lf %.8lf %.8lf %d %d", i, x, y, vX, vY, pColl, wColl); 
			string res(buffer);
			return res;
		}
}; 


class CollisionEvent 
{
	bool operator < (CollisionEvent other)
	{
		if (this->time == other.getTime()) return this->getSmallestIndex() > other.getSmallestIndex(); 
		return this->time > other.getTime();
	}

	public:
	Particle* first;
	double time;

	CollisionEvent() {}

	virtual ~CollisionEvent() {}

	CollisionEvent(Particle* first, double time)
	{
		this->first = first;
		this->time = time;
	}

	virtual void execute() {};

	double getTime()
	{
		return this->time;
	}

	double getSmallestIndex()
	{
		return (*first).getIndex();
	}
};

class ParticleCollisionEvent: public CollisionEvent
{
	public:
		bool operator == (ParticleCollisionEvent other)
		{
			int firstIndex = (*this->first).getIndex();
			int secondIndex = (*this->second).getIndex();
			int otherFirstIndex = (*other.first).getIndex();
			int otherSecondIndex = (*other.second).getIndex();
			return (firstIndex == otherSecondIndex && secondIndex == otherFirstIndex) ||
				(firstIndex == otherFirstIndex && secondIndex == otherSecondIndex);
		}
		Particle* second;


		ParticleCollisionEvent(Particle* first, Particle* second, double time)
			: CollisionEvent(first, time)
		{
			this->second = second;
		}

		void execute() 
		{
			int blockStart = blockSize * mpiRank;
			int blockEnd = blockStart + blockSize;
			// do not execute if
			// 	1. both particles belongs in the same block and
			//  2. first particle's index > second particle's index
			int firstIndex = (*this->first).getIndex();
			int secondIndex = (*this->second).getIndex();
			if (firstIndex >= blockStart && firstIndex < blockEnd && secondIndex >= blockStart && secondIndex < blockEnd 
				&& firstIndex >= secondIndex)
			{
				return;
			}
			//move them to proper position first
			first->x += this->time * first->vX;
			first->y += this->time * first->vY;
			second->x += this->time * second->vX;
			second->y += this->time * second->vY;

			//perform collision here
			//find normal vector
			double normalX = first->x - second->x;
			double normalY = first->y - second->y;
			double normalMag = sqrt(pow(normalX, 2) + pow(normalY, 2));
			normalX = normalX/normalMag; normalY = normalY/normalMag;
			double tangentX = -normalY;
			double tangentY = normalX;

			//compute velocity vectors wrt to normal and tangent
			double vFirstNormal = normalX * first->vX + normalY * first->vY;
			double vFirstTangent = tangentX * first->vX + tangentY * first->vY;
			double vSecondNormal = normalX * second->vX + normalY * second->vY;
			double vSecondTangent = tangentX * second->vX + tangentY * second->vY;

			//collision simply swaps velocities
			double temp = vFirstNormal;
			vFirstNormal = vSecondNormal;
			vSecondNormal = temp;

			first->vX = vFirstNormal * normalX + vFirstTangent * tangentX;
			first->vY = vFirstNormal * normalY + vFirstTangent * tangentY;
			second->vX = vSecondNormal * normalX + vSecondTangent * tangentX;
			second->vY = vSecondNormal * normalY + vSecondTangent * tangentY;

			//eliminate negative 0s
			if (first->vX == -0.0) first->vX = 0.0;
			if (first->vY == -0.0) first->vY = 0.0;
			if (second->vX == -0.0) second->vX = 0.0;
			if (second->vY == -0.0) second->vY = 0.0;

			//Continue to move them here
			//Check for wall collisions and stop the particle at wall if so
			double timeToMove;
			double xCollide = first->vX < 0 ? (first->x-r)/(0-first->vX) : ((double)l-r-first->x)/first->vX;
			double yCollide = first->vY < 0 ? (first->y-r)/(0-first->vY) : ((double)l-r-first->y)/first->vY;
			if (xCollide >= 1-this->time && yCollide >= 1-this->time) 
			{
				timeToMove = 1-this->time;
			}
			else
			{
				timeToMove = min(xCollide, yCollide);
			}
			first->x += timeToMove * first->vX;
			first->y += timeToMove * first->vY;

			xCollide = second->vX < 0 ? (second->x-r)/(0-second->vX) : ((double)l-r-second->x)/second->vX;
			yCollide = second->vY < 0 ? (second->y-r)/(0-second->vY) : ((double)l-r-second->y)/second->vY;
			if (xCollide >= 1-this->time && yCollide >= 1-this->time) 
			{
				timeToMove = 1-this->time;
			}
			else
			{
				timeToMove = min(xCollide, yCollide);
			}
			second->x += timeToMove * second->vX;
			second->y += timeToMove * second->vY;
			
			first->pColl++;
			second->pColl++;
		}

		double getSmallestIndex()
		{
			return (*first).getIndex() < (*second).getIndex() ? (*first).getIndex() : (*second).getIndex();
		}
};

class WallCollisionEvent: public CollisionEvent
{
	public:

		WallCollisionEvent(Particle* first, double time)
			: CollisionEvent(first, time){}

		void execute() {
			//check for x wall collisions
			//check for y wall collisions
			double xCollide = first->vX < 0 ? (first->x-r)/(0-first->vX) : ((double)l-first->x-r)/first->vX;
			double yCollide = first->vY < 0 ? (first->y-r)/(0-first->vY) : ((double)l-first->y-r)/first->vY;
			if (xCollide < yCollide) {
				first->x += xCollide * first->vX;
				first->y += xCollide * first->vY;
				first->vX = -first->vX;
				//after handling x collision, need to stop the ball at the edge of box if it collides with y too
				if (yCollide < 1) {
					first->x += (yCollide-xCollide) * first->vX;
					first->y += (yCollide-xCollide) * first->vY;
				}
				else {
					first->x += (1-xCollide) * first->vX;
					first->y += (1-xCollide) * first->vY;
				}
			}
			//collision with corner of box reverses both
			else if (xCollide == yCollide) {
				first->x += xCollide * first->vX;
				first->y += xCollide * first->vY;
				first->vX = -first->vX;
				first->vY = -first->vY;
				first->x += (1-xCollide) * first->vX;
				first->y += (1-xCollide) * first->vY;
			}
			//same as x collision but for y wall collision happening first
			else {
				first->x += yCollide * first->vX;
				first->y += yCollide * first->vY;
				first->vY = -first->vY;
				if (xCollide < 1) {
					first->x += (xCollide-yCollide) * first->vX;
					first->y += (xCollide-yCollide) * first->vY;
				}
				else {
					first->x += (1-yCollide) * first->vX;
					first->y += (1-yCollide) * first->vY;
				}
			}
			first->wColl++;
		}
};

class NoCollisionEvent: public CollisionEvent
{
	public:

		NoCollisionEvent(Particle* first)
			: CollisionEvent(first, 1.0)
		{}

		void execute() {
			//simply move the particle
			first->x += first->vX;
			first->y += first->vY;
		}
};

void sendParticles(vector<Particle *>);
void moveParticles(vector<Particle*>);
double timeParticleCollision(Particle&, Particle&);
double timeWallCollision(Particle&);

int main (int argc, char **argv)
{
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpiRank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	vector<Particle*> particles;
	string command;

	if (mpiRank == MASTER_ID)
	{
		cin >> n >> l >> r >> s >> command;

		rng.seed(rd());
		uniform_real_distribution<double> pos(r, l-r);
		uniform_real_distribution<double> velocity((double)l/(8*r), (double)l/4);

		int scanned;
		for (scanned = 0; scanned < n; ++scanned)
		{
			int index; 
			double x;
			double y;
			double vX;
			double vY; 
			int count;
			count = scanf("%d %lf %lf %lf %lf", &index, &x, &y, &vX, &vY);
			if (count == EOF || count <= 0) break;

			particles.push_back(new Particle(index, x, y, vX, vY));
		}
		for (int j = scanned; j < n; j++)
		{
			double x = pos(rng);
			double y = pos(rng);
			double vX = velocity(rng);
			double vY = velocity(rng);
			particles.push_back(new Particle(j, x, y, vX, vY));
		}
	}
	int buffer[4];
	buffer[0] = n, buffer[1] = l, buffer[2] = r, buffer[3] = s;
	MPI_Bcast(buffer, 4, MPI_INT, MASTER_ID, MPI_COMM_WORLD);
	n = buffer[0], l = buffer[1], r = buffer[2], s = buffer[3];

	blockSize = (n-1)/size + 1;
	paddedN = (n % size == 0) ? n : n + (n-n%size);
	//Allocate space on vector for particles if they are not master
	if (mpiRank != MASTER_ID)
	{
		for (int i = 0; i < n; i++) particles.push_back(new Particle(0, 0, 0, 0, 0));
	}
	for (int i = n; i < paddedN; i++) particles.push_back(new Particle(0, 0, 0, 0, 0));
	sendParticles(particles);
	auto start = chrono::high_resolution_clock::now();
	for (int i = 0; i < s; ++i)
	{
		if (mpiRank == MASTER_ID && !command.compare("print"))
		{
			for (int j = 0; j < n; ++j)
			{
				cout << i << " " << (string) (*particles[j]) << endl;
			}
		}
		moveParticles(particles);	
	}
	auto finish = std::chrono::high_resolution_clock::now();
	if (mpiRank == MASTER_ID)
	{
		for (int j = 0; j < n; ++j)
		{
			cout << s << " " << particles[j]->getFullRepresentation() << endl;
		}
	}

	double timeTaken = (double)chrono::duration_cast<chrono::nanoseconds>(finish-start).count()/1000000000;
	//if (mpiRank == MASTER_ID) cout << "Time taken: " << timeTaken << "s" << endl;

	MPI_Finalize();

	return 0;
}


void moveParticles(vector<Particle*> particles) 
{
	// index of first particle in process block
	int blockStart = blockSize * mpiRank;
		
	// time of particle-particle collisions
	vector<vector<double>> particleCollisionTimes(blockSize, vector<double>(n, 0));

	// time of particle-wall collisions
	vector<double> wallCollisionTimes(blockSize, 0);

	// calculate collision times
	for (int i = 0; i < blockSize && blockStart + i < n; ++i)
	{
		wallCollisionTimes[i] = timeWallCollision(*particles[blockStart + i]);

		for (int j = 0; j < n; ++j)
		{
			double particleCollisionTime = timeParticleCollision(*particles[blockStart + i], *particles[j]);
			particleCollisionTimes[i][j] = particleCollisionTime;
		}
	}
	
	//
	CollisionEvent* temp[blockSize] = {nullptr};
	
	int found[paddedN];
	for (int i = 0; i < paddedN; ++i) found[i] = 1;

	int globalFound = 0;
	
	int partners[paddedN];
	for (int i = 0; i < paddedN; ++i) partners[i] = -1;
	
	while (globalFound != n)
	{
		for (int i = 0; i < blockSize && blockStart + i < n; ++i)
		{
			if (found[i + blockStart] == 0)
			{
				continue;
			}
			
			// first assume no collision
			if (temp[i] != NULL) delete(temp[i]);
			partners[i + blockStart] = -1;
			temp[i] = new NoCollisionEvent(particles[i + blockStart]);

			// check for particle-wall collision
			if (wallCollisionTimes[i] < (*temp[i]).getTime() && wallCollisionTimes[i] < 1)
			{
				delete(temp[i]);
				temp[i] = new WallCollisionEvent(particles[i + blockStart], wallCollisionTimes[i]);
			}

			// check for particle-particle collision
			for (int j = 0; j < n; ++j)
			{
				if (i + blockStart == j || found[j] == 0) continue;
				double time = particleCollisionTimes[i][j];
				if (time > -1 && time < temp[i]->getTime() && time < 1) {
					delete(temp[i]);
					temp[i] = new ParticleCollisionEvent(particles[i + blockStart], particles[j], time);
					partners[i + blockStart] = j;
				}
			}
		}
		
		// gather information about each particle's partner
		int sendBuffer[blockSize];
		for (int i = 0; i < blockSize; ++i) sendBuffer[i] = partners[blockStart + i];
		MPI_Allgather(
			sendBuffer, 
			blockSize, 
			MPI_INT, 
			partners, 
			blockSize, 
			MPI_INT, 
			MPI_COMM_WORLD
		);

		for (int i = 0; i < blockSize && i + blockStart < n; ++i)
		{
			if (found[i + blockStart] == 0) continue;
			//if other 'particle' is the wall or no collision, then we can resolve it immediately
			int otherParticle = partners[i + blockStart];
			if (otherParticle == -1)
			{
				found[i + blockStart] = 0;
			}
			else
			{
				//otherwise check that other particle's first collision is with this particle and resolve this
				if (partners[otherParticle] == blockStart + i)
				{
					found[i + blockStart] = 0;
				}
			}
		}
		// gather valid collisions
		for (int i = 0; i < blockSize; i++) sendBuffer[i] = found[i + blockStart];	
		MPI_Allgather(
			sendBuffer,
			blockSize,
			MPI_INT,
			found,
			blockSize,
			MPI_INT,
			MPI_COMM_WORLD
		);
		globalFound = 0;	
		for (int i = 0; i < n; ++i) if (found[i] == 0) ++globalFound;
	}

	// apply valid collisions
	for (int i = 0; i < blockSize && blockStart + i < n; ++i)
	{
		temp[i]->execute();
	}

	for (int i = 0; i < blockSize && i + blockStart < n; ++i) delete(temp[i]);

	// gather final particle information after collision
	double sendBuffer[blockSize * 5];
	for (int i = 0; i < blockSize; ++i)
	{
		sendBuffer[i * 5] = particles[blockStart + i]->i;
		sendBuffer[i * 5 + 1] = particles[blockStart + i]->x;
		sendBuffer[i * 5 + 2] = particles[blockStart + i]->y;
		sendBuffer[i * 5 + 3] = particles[blockStart + i]->vX;
		sendBuffer[i * 5 + 4] = particles[blockStart + i]->vY;
	}
	
	double recvBuffer[paddedN * 5];
	MPI_Allgather(
		sendBuffer, 
		blockSize * 5, 
		MPI_DOUBLE, 
		recvBuffer, 
		blockSize * 5, 
		MPI_DOUBLE, 
		MPI_COMM_WORLD
	);

	for (int i = 0; i < n; ++i)
	{
		Particle* particle = particles[i];
		particle->i = recvBuffer[i * 5]; 
		particle->x = recvBuffer[i * 5 + 1];
		particle->y = recvBuffer[i * 5 + 2];
		particle->vX = recvBuffer[i * 5 + 3];
		particle->vY = recvBuffer[i * 5 + 4];
	}
}

// broadcasts all particles if process is master
void sendParticles(vector<Particle *> particles)
{
	double buffer[5 * paddedN];
	if (mpiRank == MASTER_ID) {
		for (int i = 0; i < particles.size(); ++i)
		{
			Particle* particle = particles[i];
			buffer[i * 5] = particle->i;
			buffer[i * 5 + 1] = particle->x;
			buffer[i * 5 + 2] = particle->y;
			buffer[i * 5 + 3] = particle->vX;
			buffer[i * 5 + 4] = particle->vY;
		}
	}
	MPI_Bcast(buffer, 5 * paddedN, MPI_DOUBLE, MASTER_ID, MPI_COMM_WORLD);
	if (mpiRank != MASTER_ID) {
		for (int i = 0; i < particles.size(); ++i)
		{
			Particle* particle = particles[i];
			particle->i = buffer[i * 5]; 
			particle->x = buffer[i * 5 + 1];
			particle->y = buffer[i * 5 + 2];
			particle->vX = buffer[i * 5 + 3];
			particle->vY = buffer[i * 5 + 4];
		}
	}
}

// input: 2 Particles
// output: Returns time taken before collision occurs if they collide, negative value otherwise.
double timeParticleCollision(Particle& first, Particle& second)
{

	//a, b and c are as in the quadratic formula representation.
	//t, the time taken for the 2 circles to touch, is the unknown variable we are solving for
	//by taking difference in circle centres, setting an unknown t for collision time, and then taking distance moved in time t,
	//we can solve for t such that the circle centers are <= 2r and therefore collide. 4r^2 is to solve for radius distance.
	double c = pow((first.x-second.x), 2) + pow((first.y - second.y), 2) - 4*r*r;
	double b = 2*((first.x - second.x)*(first.vX - second.vX) + (first.y - second.y)*(first.vY-second.vY));
	double a = pow((first.vX-second.vX), 2) + pow((first.vY - second.vY), 2);
	//check for solution
	if (b*b-4*a*c < 0) {
		return 100000.0;
	}
	//else if there is a solution, the one with smaller value should be the main collision. Second value is after the 2 circles phase through each other
	double solfirst = (-sqrt(b*b-4*a*c)-b)/(2*a);
	if (solfirst > 0)
	{
		return solfirst;
	}
	solfirst = (sqrt(b*b-4*a*c)-b)/(2*a);
	if (solfirst > 0)
	{
		return 0;
	}
	return 100000.0;
}

// input: 1 Particle
// output: Returns time taken before collision occurs if it collides with wall, negative value otherwise.
double timeWallCollision(Particle& particle)
{
	//check for x wall, y wall collisions
	double xCollide = particle.vX < 0 ? (particle.x-r)/(0-particle.vX) : ((double)l-particle.x-r)/particle.vX;
	double yCollide = particle.vY < 0 ? (particle.y-r)/(0-particle.vY) : ((double)l-particle.y-r)/particle.vY;
	return min(xCollide, yCollide);
}

#include <Job.h>
#include <iostream>

// The host has already created this database through its file provider.
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    ii::job::Store jobs(argv[1]);
    for (const auto &job : jobs.recent())
        std::cout << job.id << ": " << job.succeeded << '/' << job.total << '\n';
}

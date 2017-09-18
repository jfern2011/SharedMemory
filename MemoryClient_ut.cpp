#include <csignal>
#include <iostream>

#include "SharedMemory.h"
#include "util.h"

bool sigint_raised = false;

void sig_hander(int num)
{
	std::printf("caught signal [%d]. exiting...\n", num);
	std::fflush(stdout);

	sigint_raised = true;
}

int main(int argc, char** argv)
{
	SharedMemory::MemoryClient client;

	int id1, id2;
	
	AbortIfNot(client.attach("test1",
							 SharedMemory::read_write,
					 		 10, id1),
			false);
	AbortIfNot(client.attach("test2",
							 SharedMemory::read_only,
							 10, id2),
			false);

	//std::signal(SIGINT, &sig_hander);

	while (!sigint_raised)
	{
		std::cout << "> "; std::fflush(stdout);

		char _input[256];
		std::cin.getline(_input, 256);
		std::string input(_input);

		Util::str_v args;
		Util::split(input, args);

		if (Util::trim(args[0]) == "write")
		{
			if (args.size() < 2)
				std::cout << "usage: write <data>" << std::endl;
			else
			{
				AbortIfNot(client.write(id1, args[1].c_str(),
										args[1].size()), 1);
			}
		}
		else if (Util::trim(args[0]) == "read")
		{
			if (args.size() < 2)
				std::cout << "usage: read <data>" << std::endl;
			else
			{
				int size = Util::str_to_int32(args[1],10);
				if (errno == 0)
				{
					char *data = static_cast<char*>(std::malloc(size+1));
					AbortIf(data == NULL, 1);

					AbortIfNot(client.read(id2, data, size), 1);
					data[size] = '\0';

					std::printf("received '%s'\n", data);
					std::fflush(stdout);
					std::free(data);
				}
				else
				{
					std::cout << "cannot convert " << args[1]
						<< std::endl;
					errno = 0;
				}
			}
		}
		else if (Util::trim(args[0]) == "quit")
		{
			break;
		}
		else
			std::cout << "unknown command: " << args[0]
				<< std::endl;
	}

	return 0;	
}

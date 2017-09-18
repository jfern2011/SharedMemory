#include <csignal>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include "util.h"

#include "SharedMemory.h"

bool sigint_raised = false;

void sig_hander(int num)
{
	std::printf("caught signal [%d]. exiting...\n", num);
	std::fflush(stdout);

	sigint_raised = true;
}

namespace SharedMemory
{
	class MemoryManger_ut
	{

	public:

		MemoryManger_ut()
		{
		}

		~MemoryManger_ut()
		{
		}

		int allocate(size_t size)
		{
			int id = _manager.allocate(size);
			if (id == -1)
			{
				std::printf("Not enough space. \n");
				std::fflush(stdout);
			}
			else
				print();

			return id;
		}

		void free(int id)
		{
			if (!_manager.free(id))
			{
				std::printf("Invalid ID: %d\n", id);
				std::fflush(stdout);
			}
			else
				print();
		}

		bool init(void* addr, size_t size)
		{
			AbortIfNot(_manager.init(addr, size),
				false);

			_buf_size = size;
			return true;
		}

		void print()
		{
			auto in_use = _manager._in_use;
			auto vacant = _manager._vacant;

			auto all = in_use;
			all.insert(all.end(), vacant.begin(), vacant.end());

			for (auto iter = all.begin(), end = all.end();
				 iter != end; ++iter)
			{
				std::printf(" | %2d: %2lu",iter->id,iter->size);
			}
			std::printf(" |\n");
			std::fflush(stdout);
		}

		bool run()
		{
			while (true)
			{
				std::cout << "> "; std::fflush(stdout);

				char _input[256];
				std::cin.getline(_input, 256);
				std::string input(_input);

				Util::str_v args;
				Util::split(input, args);

				if (Util::trim(args[0]) == "allocate")
				{
					if (args.size() < 2)
						std::cout << "usage: allocate <size>" << std::endl;
					else
					{
						int size = Util::str_to_int32(args[1],10);
						if (errno == 0)
						{
							allocate(static_cast<size_t>(size));
						}
						else
						{
							std::cout << "cannot convert " << args[1]
								<< std::endl;
							errno = 0;
						}
					}
				}
				else if (Util::trim(args[0]) == "free")
				{
					if (args.size() < 2)
						std::cout << "usage: free <id>" << std::endl;
					else
					{
						int id = Util::str_to_int32(args[1],10);
						if (errno == 0)
						{
							free(id);
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
					break;
				else
					std::cout << "unknown command: " << args[0]
						<< std::endl;
			}

			return true;
		}

	private:

		size_t _buf_size;
		SharedMemory::MemoryManager
			_manager;
	};
}

bool run_MemoryManager_ut(int argc, char** argv)
{
	SharedMemory::MemoryManger_ut test;

	void* addr = NULL;
	if (argc > 1)
	{
		int size = Util::str_to_int32(argv[1],10);
		if (errno == 0)
		{
			addr = std::malloc(size);
			if (addr == NULL)
				std::cout << "error: malloc()" << std::endl;
			else
			{
				test.init(addr, size);
				test.run();
				std::free(addr);
			}
		}
		else
			std::cout << "errno = " << errno
				<< std::endl;
	}
	else
		std::cout << "usage: " << argv[0] << " <pool size>"
			<< std::endl;

	return true;
}

int main(int argc, char** argv)
{
	//run_MemoryManager_ut(argc, argv);

	SharedMemory::RemoteMemory remote1, remote2;
	
	AbortIfNot(remote1.create("test1",
							  SharedMemory::read_write,
					 		  10),
			false);
	AbortIfNot(remote2.create("test2",
							  SharedMemory::read_only,
							  10),
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
				// Clients read from this
				AbortIfNot(remote2.write(args[1].c_str(),
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

					// Clients write to this
					AbortIfNot(remote1.read(data, size), 1);
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

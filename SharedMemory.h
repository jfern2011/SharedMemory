#ifndef __SHARED_MEMORY_H__
#define __SHARED_MEMORY_H__

#include <cstring>
#include <fcntl.h>
#include <list>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#include "abort.h"

namespace SharedMemory
{
	/**
	 ******************************************************************
	 *
	 * @class MemoryManager
	 *
	 * Manages the use of a fixed-size memory pool. If a sufficiently
	 * small pool is allocated, repeated memory allocations and
	 * deletions may make it impossible to service further memory
	 * requests without defragmenting the storage space, which
	 * might slow down your application. If that's the case then just
	 * preallocate some more
	 *
	 ******************************************************************
	 */
	class MemoryManager
	{
		friend class MemoryManger_ut;

		struct Block
		{
			Block(int _id, size_t _offset, size_t _size)
				: id(_id),
				  offset(_offset),
				  size(_size)
			{
			}

			int    id;     /*!< Memory block ID */
			size_t offset; /*!< Buffer offset   */
			size_t size;   /*!< Block size      */
		};

	public:

		/**
		 * Constructor
		 */
		MemoryManager()
			: _addr(NULL), _in_use(), _is_init(false), _last_index(0),
			  _size(0), _vacant()
		{
		}

		/**
		 * Destructor
		 */
		~MemoryManager()
		{
		}

		/**
		 * Allocate a block of memory. If \a size is zero, or if there
		 * is no space left, -1 is returned
		 *
		 * @param[in] size The number of bytes to allocate
		 *
		 * @return A unique id by which to reference this block, or on
		 *         error, -1
		 */
		int allocate(size_t size)
		{
			AbortIfNot( _is_init, -1);
			AbortIf(size > _size, -1);

			if (size == 0 || _vacant.empty())
				return -1;

			/*
			 * First pass: search through the list of vacancies for an
			 * element of at least 'size' bytes
			 */
			for (auto iter = _vacant.begin(), end = _vacant.end();
				 iter != end; ++iter)
			{
				if (iter->size >= size)
					return _allocate(iter, size);
			}

			/*
			 * Second attempt: We were unable to find a vacancy large
			 * enough to accommodate the request, so go ahead and
			 * defrag. This will consolidate all free elements into a
			 * single blob which is hopefully big enough:
			 */
			defrag();

			auto iter =  _vacant.begin();

			if (iter->size >= size)
				return
					_allocate(iter,size);

			return -1;
		}

		/**
		 * Free a block of memory
		 *
		 * @param[in] id The unique ID returned by \ref allocate() by
		 *               which to reference the block
		 *
		 * @return True on success
		 */
		bool free(int id)
		{
			AbortIfNot( _is_init, false );

			std::list<Block>::const_iterator iter;
			AbortIfNot(lookup(id, iter),
					false);

			_vacant.push_back( Block(-1, iter->offset, iter->size) );
				_in_use.erase(iter);

			return true;
		}

		/**
		 * Initialize
		 *
		 * @param[in] addr The address of the buffer which will serve
		 *                 as our memory pool
		 * @param[in] size The size of the buffer, in bytes
		 *
		 * @return True on success
		 */
		bool init(void* addr, size_t size)
		{
			AbortIf(_is_init, false);
			AbortIf(addr== NULL || size == 0, false);

			_addr = addr;
			_size = size;

			_vacant.push_back( Block(-1, 0, _size) );

			_is_init = true;
			return true;
		}

		/**
		 * Read the contents of an allocated memory block
		 * 
		 * @param[in] id     The unique ID of this block returned by
		 *                   /ref allocate()
		 * @param[in] buf    The buffer to read into
		 * @param[in] nbytes The number of bytes to copy into /a buf
		 *
		 * @return True on success
		 */
		bool read(int id, void* buf, size_t nbytes) const
		{
			AbortIfNot( _is_init, false );

			std::list<Block>::const_iterator iter;
			AbortIfNot(lookup(id, iter),
					false);
			AbortIf( iter->size <  nbytes,
					 false);

			void* addr =
				static_cast<char*>(_addr) + iter->offset;

			std::memcpy(buf, addr, nbytes);

			return true;
		}

		/**
		 * Write to an allocated memory block
		 * 
		 * @param[in] id     The unique ID of this block returned by
		 *                   /ref allocate()
		 * @param[in] buf    The buffer to copy from
		 * @param[in] nbytes The number of bytes to copy from /a buf
		 *
		 * @return True on success
		 */
		bool write(int id,
					const void* buf, size_t nbytes) const
		{
			AbortIfNot( _is_init, false );

			std::list<Block>::const_iterator iter;
			AbortIfNot(lookup(id, iter),
					false);
			AbortIf( iter->size <  nbytes,
					 false);

			void* addr =
				static_cast<char*>(_addr) + iter->offset;

			std::memcpy(addr, buf, nbytes);

			return true;
		}

	private:

		/**
		 * Allocate memory. This is called once it's been determined
		 * that there's enough space available
		 *
		 * @param[in] iter An iterator to the next vacancy
		 *
		 * @return A unique ID by which to reference the newly
		 *         allocated memory
		 */
		int _allocate(std::list<Block>::iterator& iter,
					  size_t size)
		{
			size_t rem = iter->size - size;

			/*
			 * Update both the list of free elements and the list of
			 * blocks in use:
			 */
			_in_use.push_back(Block(_last_index, iter->offset,
									size));

			if (rem == 0)
				_vacant.erase(iter);
			else
			{
				iter->size = rem;
					iter->offset += size;
			}

			return
				_last_index++;
		}

		/**
		 * Defragment the memory pool. This is called whenever there
		 * is space left, but the unused blocks are scattered
		 * throughout the internal buffer. These are consolidated
		 * into one big chunk of free memory, and all used blocks
		 * are also placed contiguously. The goal is to service
		 * an allocation request that needs a larger block than what
		 * can be obtained from the scattered pieces
		 */
		void defrag()
		{
			size_t offset = 0;
			for (auto iter = _in_use.begin(), end = _in_use.end();
				 iter != end; ++iter)
			{
				char* addr_c = static_cast<char*>(_addr);
				void* addr =  addr_c + iter->offset;

				if (offset != iter->offset)
					std::memcpy(addr_c+ offset, addr, iter->size);

				iter->offset = offset;
				offset += iter->size;
			}

			_vacant.clear();
			_vacant.push_back(Block(-1, offset,
						_size-offset));
		}

		/**
		 *  Look up a memory block currently in use by ID, returning
		 *  an iterator to the element
		 *
		 * @param[in]  id   The ID
		 * @param[out] iter An iterator to the element with ID \a
		 *                  id
		 *
		 * @return True if found, false otherwise
		 */
		inline bool lookup(int id, std::list<Block>::const_iterator&
							iter) const
		{
			std::list<Block>::const_iterator end =
				_in_use.end();

			for (iter=_in_use.begin(); iter != end; ++iter)
			{
				if (iter->id == id) return true;
			}

			return false;
		}

		void*  _addr;
		std::list<Block>
			   _in_use;
		bool   _is_init;
		int    _last_index;
		size_t _size;
		std::list<Block>
			   _vacant;
	};

	/**
	 *  Permissions granted to external processes wishing to use this
	 *  resource
	 */
	typedef enum
	{
		none       = 0, /*!< No access         */
		read_only  = 1, /*!< Read-only access  */
		read_write = 2  /*!< Read-write access */

	} access_t;


	/**
	 ******************************************************************
	 *
	 * @class RemoteMemory
	 *
	 * Creates a shared memory object which client processes may read
	 * from/write to
	 *
	 ******************************************************************
	 */
	class RemoteMemory
	{

	public:

		/**
		 * Constructor
		 */
		RemoteMemory()
			: _access( none ),
			  _addr(NULL),
			  _fd(-1),
			  _is_init(false),
			  _manager(),
			  _mem_id(-1),
			  _name(""),
			  _size(0)
		{
		}

		/**
		 * Destructor
		 */
		~RemoteMemory()
		{
			if (_is_init) destroy();
		}

		/**
		 * Create the shared object
		 *
		 * @param[in] name   The name to assign to the object. Note
		 *                   shm_open() requires a leading '/',
		 *                   but that will be added here if it's
		 *                   missing
		 * @param[in] access Permissions to give to processes using
		 *                   this resource
		 * @param[in] size   The total number of bytes to be shared
		 *
		 * @return True on success
		 */
		bool create(const std::string& name, access_t access,
					size_t size)
		{
			AbortIfNot(init(access, name, size),
				false);

			const int oflag = O_CREAT | O_RDWR | O_EXCL;
			mode_t mode     = S_IRWXU;
			const int prot  = 	 PROT_READ | PROT_WRITE;

			switch (_access)
			{
			case read_only:
				mode  |= (S_IRGRP | S_IROTH);
				break;
			case read_write:
				mode  |= (S_IRWXG | S_IRWXO);
			}

			const int fd = ::shm_open(_name.c_str(), oflag, mode);

			AbortIf(fd == -1, false);
			_fd = fd;

			AbortIf(::ftruncate(_fd, _size) == -1,
				false);

			_addr = ::mmap(NULL, _size, prot, MAP_SHARED, _fd, 0);
			AbortIf(errno, false);

			AbortIfNot(_manager.init(_addr, _size),
				false);

			_mem_id =  _manager.allocate ( _size );
			AbortIf(_mem_id == -1,
				false);

			_is_init = true;
			return true;
		}

		/**
		 * Remove the shared object, unmap it from memory, and close
		 * the file descriptor
		 *
		 * @return True on success
		 */
		bool destroy()
		{
			AbortIfNot(_is_init, false);

			AbortIf(::munmap ( _addr, _size )   == -1,
				 false);
			AbortIf(::shm_unlink(_name.c_str()) == -1,
				false);

			AbortIf(::close(_fd) == -1 ,
				false);

			_is_init = false;
			return true;
		}

		/**
		 * Read data from the block of memory associated with ID /a
		 * id into /a buf
		 *
		 * @param[in] buf  The buffer to read into
		 * @param[in] size The total number of bytes to read
		 *
		 * @return True on success
		 */
		bool read( void* buf, size_t size ) const
		{
			AbortIfNot(_is_init, false);
			AbortIfNot(_manager.read(_mem_id, buf, size),
				false);

			return true;
		}

		/**
		 * Write data from /a buf to the block of memory associated
		 * with ID /a id
		 *
		 * @param[in] buf  The buffer to write from
		 * @param[in] size The total number of bytes to write
		 *
		 * @return True on success
		 */
		bool write( const void* buf, size_t size ) const
		{
			AbortIfNot( _is_init, false );

			/*
			 * Lock this resource into physical memory while making
			 * changes
			 */
			AbortIf(::mlock( _addr, _size ) == -1,
				false);

			AbortIfNot(
				_manager.write(_mem_id, buf,size),
				false);

			AbortIf(::munlock(_addr, _size) == -1,
				false);

			/*
			 * Flush changes back to the file system. Note that this
			 * commits the entire file
			 */
			AbortIf(::msync(_addr, _size,
						MS_SYNC | MS_INVALIDATE) == -1,
				false);

			return true;
		}

	private:

		/*
		 * Assign defaults to members
		 */
		bool init(access_t access, const std::string& name,
				  size_t size)
		{
			AbortIf(_is_init, false);

			AbortIfNot(name.size() > 0, false );

			_access = access;
			_fd     = -1;
			_name   = name;
			_size   = size;

			/*
			 * Note: shm_open() requires the name to begin
			 *       with '/'
			 */
			if (name[0] != '/')
			{
				_name = std::string("/") + name;
			}

			return true;
		}

		access_t      _access;
		void*         _addr;
		int           _fd;
		bool          _is_init;
		MemoryManager _manager;
		int           _mem_id;
		std::string   _name;
		size_t        _size;

	};


	/**
	 ******************************************************************
	 *
	 * @class MemoryClient
	 *
	 * Opens up and maps one or more shared memory objects for reading
	 * and/or writing
	 *
	 ******************************************************************
	 */
	class MemoryClient
	{
		struct Server
		{
			Server( access_t _access, void* _addr, int _fd,
					int _id, const std::string& _name,
					size_t _size)
				: access( _access ), addr( _addr), fd(_fd),
				  id(_id), name(_name), mem_id(-1),
				  size(_size)
			{
			}

			bool init()
			{
				AbortIfNot( manager.init(addr,size), false );
				return true;
			}

			access_t  access;
			void* addr;
			int fd;
			int id;
			MemoryManager
					 manager;
			int mem_id;
			std::string name;
			size_t size;
		};

	public:

		/**
		 * Constructor
		 */
		MemoryClient() : _last_id(0), _servers()
		{
		}

		/**
		 * Destructor
		 */
		~MemoryClient()
		{
			for (size_t i = _servers.size(); i > 0; i--)
			{
				destroy((_servers.begin())->id);
			}
		}

		/**
		 * Attach to a shared memory object
		 *
		 * @param[in]  name   The name of an existing shared memory
		 *                    object
		 * @param[in]  access Permissions for this resource
		 * @param[in]  size   Number of bytes to use
		 * @param[out] id     The unique id to reference the shared
		 *                    object by
		 *
		 * @return True on success
		 */
		bool attach(const std::string& name, access_t access,
					 size_t size, int& id)
		{
			int oflag = 0, prot;

			switch (access)
			{
			case read_only:
				oflag =  O_RDONLY;
				prot  = PROT_READ;
				break;
			case read_write:
				oflag = O_RDWR;
				prot  =
					PROT_READ | PROT_WRITE;
				break;
			default:
				prot  = PROT_NONE;
			}

			AbortIf(name.size() == 0,
				false);

			/*
			 * Make sure we are not trying to re-attach to the same
			 * shared object
			 */
			std::string real_name;
			if (name[0] != '/')
				real_name = std::string("/") + name;
			else
				real_name = name;

			for (auto iter =_servers.begin(), end = _servers.end();
				 iter != end; ++iter)
			{
				AbortIf(iter->name == real_name,
					false);
			}

			int fd = ::shm_open(real_name.c_str(), oflag, 0);
			AbortIf(fd == -1, false);

			void* addr =
				 ::mmap(NULL, size, prot, MAP_SHARED, fd, 0);
			AbortIf(errno, false);

			Server&& server =
				Server(access, addr, fd, _last_id, real_name,
						size);

			/*
			 * Initialize the manager for this resource. No further
			 * memory allocations or deletions are performed
			 * since that could potentially misalign our memory and
			 * the RemoteMemory's copy
			 */
			AbortIfNot ( server.init(),
				false);

			server.mem_id = server.manager.allocate( size );
			AbortIf(server.mem_id < 0,
				false);

			_servers.push_back(server);

			id = _last_id++;
			return true;
		}

		/**
		 * Remove a shared object, unmap it from memory, and close
		 * the file descriptor
		 *
		 * @param [in] id A unique ID returned by /ref attach() by
		 *                which to reference the object
		 *
		 * @return True on success
		 */
		bool destroy(int id)
		{
			std::list<Server>::const_iterator iter;
			AbortIfNot(lookup(id, iter),
				false);

			AbortIf(::munmap (iter->addr,iter->size) == -1,
				false);
			AbortIf(::close(iter->fd) == -1 ,
				false);

			_servers.erase(iter);
			return true;
		}

		/**
		 * Read data from a block of memory
		 *
		 * @param[in] id   A unique ID returned by /ref attach() by
		 *                 which to reference the object
		 * @param[in] buf  The buffer to read into
		 * @param[in] size The total number of bytes to read
		 *
		 * @return True on success
		 */
		bool read( int id, void* buf, size_t size ) const
		{
			std::list<Server>::const_iterator iter;
			AbortIfNot(lookup(id, iter),
				false);
			AbortIfNot(iter->manager.read(iter->mem_id, buf,
				size), false);

			return true;
		}

		/**
		 * Write data from /a buf to the block of memory referenced
		 * by /a id
		 *
		 * @param[in] id   A unique ID returned by /ref attach()
		 *                 by which to reference the object
		 * @param[in] buf  The buffer to copy from
		 * @param[in] size The total number of bytes to commit to
		 *                 the shared object
		 *
		 * @return True on success
		 */
		bool write( int id, const void* buf, size_t size ) const
		{
			std::list<Server>::const_iterator iter;
			AbortIfNot(lookup(id, iter),
				false);

			AbortIf(iter->access != read_write,
				false);

			/*
			 * Lock this resource into physical memory while making
			 * changes
			 */
			AbortIf(::mlock( iter->addr, iter->size ) == -1,
				false);

			AbortIfNot(iter->manager.write(iter->mem_id,
				buf, size), false);

			AbortIf(::munlock(iter->addr, iter->size) == -1,
				false);

			/*
			 * Flush changes back to the file system. Note that this
			 * commits the entire file
			 */
			AbortIf(::msync(iter->addr, iter->size,
						MS_SYNC | MS_INVALIDATE) == -1,
				false);

			return true;
		}

	private:

		/**
		 *  Look up a shared memory object by ID, returning an
		 *  iterator to the object
		 *
		 * @param[in] id    An ID returned by /ref attach()
		 *                  to identify the shared object
		 * @param[out] iter An iterator pointing the shared
		 *                  object
		 *
		 * @return True if found
		 */
		inline bool lookup(int id,
				std::list<Server>::const_iterator& iter) const
		{
			std::list<Server>::const_iterator end =
				_servers.end();

			for (iter = _servers.begin(); iter != end; ++iter)
			{
				if (iter->id == id) return true;
			}

			return false;
		}


		int _last_id;
		std::list<Server>
			_servers;
	};
}

#endif

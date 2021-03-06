/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PASSENGER_APPLICATION_POOL_H_
#define _PASSENGER_APPLICATION_POOL_H_

#include <boost/shared_ptr.hpp>
#include <sys/types.h>

#include "Application.h"

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * A persistent pool of Applications.
 *
 * Spawning application instances, especially Ruby on Rails ones, is a very expensive operation.
 * Despite best efforts to make the operation less expensive (see SpawnManager),
 * it remains expensive compared to the cost of processing an HTTP request/response.
 * So, in order to solve this, some sort of caching/pooling mechanism will be required.
 * ApplicationPool provides this.
 *
 * Normally, one would use SpawnManager to spawn a new RoR/Rack application instance,
 * then use Application::connect() to create a new session with that application
 * instance, and then use the returned Session object to send the request and
 * to read the HTTP response. ApplicationPool replaces the first step with
 * a call to Application::get(). For example:
 * @code
 *   ApplicationPool pool = some_function_which_creates_an_application_pool();
 *   
 *   // Connect to the application and get the newly opened session.
 *   Application::SessionPtr session(pool->get("/home/webapps/foo"));
 *   
 *   // Send the request headers and request body data.
 *   session->sendHeaders(...);
 *   session->sendBodyBlock(...);
 *   // Done sending data, so we shutdown the writer stream.
 *   session->shutdownWriter();
 *
 *   // Now read the HTTP response.
 *   string responseData = readAllDataFromSocket(session->getStream());
 *   // Done reading data, so we shutdown the reader stream.
 *   session->shutdownReader();
 *
 *   // This session has now finished, so we close the session by resetting
 *   // the smart pointer to NULL (thereby destroying the Session object).
 *   session.reset();
 *
 *   // We can connect to an Application multiple times. Just make sure
 *   // the previous session is closed.
 *   session = app->connect("/home/webapps/bar")
 * @endcode
 *
 * Internally, ApplicationPool::get() will keep spawned applications instances in
 * memory, and reuse them if possible. It wil* @throw l try to keep spawning to a minimum.
 * Furthermore, if an application instance hasn't been used for a while, it
 * will be automatically shutdown in order to save memory. Restart requests are
 * honored: if an application has the file 'restart.txt' in its 'tmp' folder,
 * then get() will shutdown existing instances of that application and spawn
 * a new instance (this is useful when a new version of an application has been
 * deployed). And finally, one can set a hard limit on the maximum number of
 * applications instances that may be spawned (see ApplicationPool::setMax()).
 *
 * Note that ApplicationPool is just an interface (i.e. a pure virtual class).
 * For concrete classes, see StandardApplicationPool and ApplicationPoolServer.
 * The exact pooling algorithm depends on the implementation class.
 *
 * @ingroup Support
 */
class ApplicationPool {
public:
	virtual ~ApplicationPool() {};
	
	/**
	 * Open a new session with the application specified by <tt>appRoot</tt>.
	 * See the class description for ApplicationPool, as well as Application::connect(),
	 * on how to use the returned session object.
	 *
	 * Internally, this method may either spawn a new application instance, or use
	 * an existing one.
	 *
	 * If <tt>lowerPrivilege</tt> is true, then any newly spawned application
	 * instances will have lower privileges. See SpawnManager::SpawnManager()'s
	 * description of <tt>lowerPrivilege</tt> and <tt>lowestUser</tt> for details.
	 *
	 * @param appRoot The application root of a RoR application, i.e. the folder that
	 *             contains 'app/', 'public/', 'config/', etc. This must be a valid
	 *             directory, but does not have to be an absolute path.
	 * @param lowerPrivilege Whether to lower the application's privileges.
	 * @param lowestUser The user to fallback to if lowering privilege fails.
	 * @param environment The RAILS_ENV/RACK_ENV environment that should be used. May not be empty.
	 * @param spawnMethod The spawn method to use. Either "smart" or "conservative".
 	 *                    See the Ruby class SpawnManager for details.
 	 * @param appType The application type. Either "rails" or "rack".
	 * @return A session object.
	 * @throw SpawnException An attempt was made to spawn a new application instance, but that attempt failed.
	 * @throw BusyException The application pool is too busy right now, and cannot
	 *       satisfy the request. One should either abort, or try again later.
	 * @throw IOException Something else went wrong.
	 * @throw thread_interrupted
	 * @note Applications are uniquely identified with the application root
	 *       string. So although <tt>appRoot</tt> does not have to be absolute, it
	 *       should be. If one calls <tt>get("/home/foo")</tt> and
	 *       <tt>get("/home/../home/foo")</tt>, then ApplicationPool will think
	 *       they're 2 different applications, and thus will spawn 2 application instances.
	 */
	virtual Application::SessionPtr get(const string &appRoot, bool lowerPrivilege = true,
		const string &lowestUser = "nobody", const string &environment = "production",
		const string &spawnMethod = "smart", const string &appType = "rails") = 0;
	
	/**
	 * Clear all application instances that are currently in the pool.
	 *
	 * This method is used by unit tests to verify that the implementation is correct,
	 * and thus should not be called directly.
	 */
	virtual void clear() = 0;
	
	virtual void setMaxIdleTime(unsigned int seconds) = 0;
	
	/**
	 * Set a hard limit on the number of application instances that this ApplicationPool
	 * may spawn. The exact behavior depends on the used algorithm, and is not specified by
	 * these API docs.
	 *
	 * It is allowed to set a limit lower than the current number of spawned applications.
	 */
	virtual void setMax(unsigned int max) = 0;
	
	/**
	 * Get the number of active applications in the pool.
	 *
	 * This method exposes an implementation detail of the underlying pooling algorithm.
	 * It is used by unit tests to verify that the implementation is correct,
	 * and thus should not be called directly.
	 */
	virtual unsigned int getActive() const = 0;
	
	/**
	 * Get the number of active applications in the pool.
	 *
	 * This method exposes an implementation detail of the underlying pooling algorithm.
	 * It is used by unit tests to verify that the implementation is correct,
	 * and thus should not be called directly.
	 */
	virtual unsigned int getCount() const = 0;
	
	/**
	 * Set a hard limit on the number of application instances that a single application
	 * may spawn in this ApplicationPool. The exact behavior depends on the used algorithm, 
	 * and is not specified by these API docs.
	 *
	 * It is allowed to set a limit lower than the current number of spawned applications.
	 */
	virtual void setMaxPerApp(unsigned int max) = 0;
	
	/**
	 * Get the process ID of the spawn server that is used.
	 *
	 * This method exposes an implementation detail. It is used by unit tests to verify
	 * that the implementation is correct, and thus should not be used directly.
	 */
	virtual pid_t getSpawnServerPid() const = 0;
};

typedef shared_ptr<ApplicationPool> ApplicationPoolPtr;

}; // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_H_ */

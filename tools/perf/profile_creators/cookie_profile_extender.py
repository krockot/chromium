# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import multiprocessing
import os
import sqlite3

import page_sets

from profile_creators import fast_navigation_profile_extender
from profile_creators import profile_safe_url_list

class CookieProfileExtender(
    fast_navigation_profile_extender.FastNavigationProfileExtender):
  """This extender fills in the cookie database.

  By default, Chrome purges the cookie DB down to 3300 cookies. However, it
  won't purge cookies accessed in the last month. This means the extender needs
  to be careful not to create an artificially high number of cookies.
  """
  _COOKIE_DB_EXPECTED_SIZE = 3300

  def __init__(self):
    # The rate limiting factors are fetching network resources and executing
    # javascript. There's not much to be done about the former, and having one
    # tab per logical core appears close to optimum for the latter.
    maximum_batch_size = multiprocessing.cpu_count()
    super(CookieProfileExtender, self).__init__(maximum_batch_size)

    # A list of urls that have not yet been navigated to. This list will shrink
    # over time. Each navigation will add a diminishing number of new cookies,
    # since there's a high probability that the cookie is already present.
    self._page_set = page_sets.ProfileSafeUrlsPageSet()
    urls = []
    for user_story in self._page_set.user_stories:
      urls.append(user_story.url)
    self._navigation_urls = urls

  def GetUrlIterator(self):
    """Superclass override."""
    return iter(self._navigation_urls)

  def ShouldExitAfterBatchNavigation(self):
    """Superclass override."""
    return self._IsCookieDBFull()

  def WebPageReplayArchivePath(self):
    return self._page_set.WprFilePathForUserStory(
        self._page_set.user_stories[0])

  def FetchWebPageReplayArchives(self):
    """Superclass override."""
    self._page_set.wpr_archive_info.DownloadArchivesIfNeeded()

  @staticmethod
  def _CookieCountInDB(db_path):
    """The number of cookies in the db at |db_path|."""
    connection = sqlite3.connect(db_path)
    try:
      cursor = connection.cursor()
      cursor.execute("select count(*) from cookies")
      cookie_count = cursor.fetchone()[0]
    except:
      raise
    finally:
      connection.close()
    return cookie_count

  def _IsCookieDBFull(self):
    """Chrome does not immediately flush cookies to its database. It's possible
    that this method will return a false negative."""
    cookie_db_path = os.path.join(self.profile_path, "Default", "Cookies")
    try:
      cookie_count = CookieProfileExtender._CookieCountInDB(cookie_db_path)
    except sqlite3.OperationalError:
      # There will occasionally be contention for the SQLite database. This
      # shouldn't happen often, so ignore the errors.
      return False

    return cookie_count > CookieProfileExtender._COOKIE_DB_EXPECTED_SIZE

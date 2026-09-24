#
# Copyright (c) 2025 Mindaugas Rasiukevicius <rmind at noxt eu>
# All rights reserved.
#
# Use is subject to license terms, as specified in the LICENSE file.
#


class NxsFatalError(Exception):
    pass


class NxsSystemError(OSError):
    pass


class NxsInvalidValueError(ValueError):
    pass


class NxsResourceExistsError(Exception):
    pass


class NxsResourceMissingError(Exception):
    pass


class NxsResourceLimitError(Exception):
    pass

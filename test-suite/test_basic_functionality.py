import pytest
import os
from subprocess import check_output, CalledProcessError


def test_app_exists():
	if not os.path.exists('./build/ddm'):
		assert False, "Have you built ddm?"


@pytest.mark.timeout(1)  # if it takes more than 1 second something is wrong
def test_app_displays_help_and_exits():
	with pytest.raises(CalledProcessError):  # ddm returns 100 on help message
		output = check_output(['./build/ddm', '-h'])
		assert 'diff'.encode() in output, 'diff command in help message'

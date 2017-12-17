#include "common.h"

/*
 * KeRestoreFloatingPointState
 *
 * Import Number:      139
 * Calling Convention: stdcall
 * Parameter 0:        PKFLOATING_SAVE FloatSave
 * Return Type:        NTSTATUS
 */
int Xbox::KeRestoreFloatingPointState()
{
	K_ENTER_STDCALL();
	K_INIT_ARG(PKFLOATING_SAVE, FloatSave);
	NTSTATUS rval;

	K_EXIT_WITH_VALUE(rval);
	return ERROR_NOT_IMPLEMENTED;
}
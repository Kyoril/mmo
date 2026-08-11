mmo_add_test(xml_handler_tests
	base
	xml_handler)

# NOTE: GetValueAsInt/GetValueAsFloat ASSERT when the value fails to parse at all, so that
# path aborts a debug build instead of returning the caller default. Not exercised by the
# suite; the graceful-default behaviour of GetValueAsBool is.

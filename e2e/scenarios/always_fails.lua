-- Negative control: proves that assertion failures produce exit code 1
-- and a meaningful transcript. This scenario is expected to FAIL.
-- The suite runner (e2e_run.ps1) knows it by name and inverts the result.

Log("This scenario intentionally fails to verify the failure plumbing.")

Assert(1 == 2, "intentional failure: one should equal two")

# Mandatory Development Rules — CI Quota and Handoff

> **Read this file before any development task.** These rules apply to all code assistants and contributors working in this repository.

## Protect GitHub Actions free quota

- Do not add automatic GitHub Actions triggers (`push`, `pull_request`, `schedule`, `workflow_run`) or trigger fan-out unless the owner explicitly approves a documented monthly runner-minute estimate.
- Default test/CI workflows to manual execution. Prefer local tests or an already-approved development environment. Never claim an alternative runner is free without checking its limits and billing.
- Before any workflow change, estimate monthly usage: runs/month × jobs/run × matrix variants × minutes/job; include retries, deployment-triggered checks, and parallel jobs. Record the estimate and official quota/pricing source/date in the development log.
- Use narrow path filters where safe, bounded timeouts, concurrency cancellation, dependency caching, and minimal permissions. These reduce waste but do not replace cost estimation.
- Do not run workflows just to experiment if a lower-cost validation route exists. Documentation-only changes should not trigger code tests unless documentation validation is relevant.
- Preserve required production deployment/release triggers. Do not disable production deployment or recovery checks without explicit approval and a safe replacement.
- Report statuses honestly: configured, run, passed, failed, not run, or not verified. Never claim tests/deployments passed or savings were measured without run evidence.

## Required handoff record

For each meaningful change, update the project's context/handoff document and development log with goal, files changed, workflow cost assessment (if applicable), tests actually run and outcomes, commit/PR, deployment evidence, blockers, risks, and rollback notes. At the start of a task, read the context index and inspect only relevant files; do not scan the entire repository unless architecture/shared assumptions are changing.

## Canonical shared policy

The full cross-project policy is maintained in [AIOS Enterprise Pro — Shared Development Handoff](https://github.com/and14122001/-AIOSEnterprisePro/blob/main/docs/SHARED_DEVELOPMENT_HANDOFF.md). Read it before CI/workflow changes. Keep this local file in sync with its mandatory rules.

## Important

Adding this policy does not prove existing workflows are safe or cheap. Audit existing workflow triggers before running them. GitHub plan limits and pricing may change; check the current official billing documentation before changing automation: https://docs.github.com/en/billing/managing-billing-for-github-actions/about-billing-for-github-actions

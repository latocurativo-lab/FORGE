import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isDirty } from '../src/state.mjs';

test('no file is never dirty', () => assert.equal(isDirty('', 'changed', ''), false));
test('a changed open file is dirty', () => assert.equal(isDirty('a.txt', 'new', 'old'), true));
test('restoring the saved content clears dirty state', () => assert.equal(isDirty('a.txt', 'old', 'old'), false));

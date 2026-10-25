/* Positron SDK v1. Only installed after native storage.sql authorization. */
(function (global) {
    'use strict';
    var services = global.PositronServices;
    if (!services || typeof services.request !== 'function' || global.Positron) {
        throw new Error('Positron SDK unavailable');
    }
    function send(request, callback) {
        if (typeof callback !== 'function') {
            throw new TypeError('A callback(ok, result) is required');
        }
        return services.request('storage.sql', request, callback);
    }
    function statement(method, sql, params, callback) {
        if (typeof sql !== 'string' || !Array.isArray(params)) {
            throw new TypeError('SQL string and typed parameter array required');
        }
        return send({version: 1, method: method, sql: sql, params: params}, callback);
    }
    var db = Object.freeze({
        query: function (sql, params, callback) {
            return statement('query', sql, params, callback);
        },
        execute: function (sql, params, callback) {
            return statement('execute', sql, params, callback);
        },
        transaction: function (statements, callback) {
            if (!Array.isArray(statements)) {
                throw new TypeError('A typed statement array is required');
            }
            return send({version: 1, method: 'transaction', statements: statements}, callback);
        }
    });
    Object.defineProperty(global, 'Positron', {
        value: Object.freeze({apiVersion: 1, db: db}), writable: false, configurable: false
    });
}(window));

#ifndef HYPERAMP_CLIENT_H
#define HYPERAMP_CLIENT_H

/**
 * HyperAMP Client - Optimized client for HyperAMP secure service communication
 * 
 * This module provides a simplified interface for HyperAMP client operations,
 * reducing unnecessary logging and improving performance.
 */

/**
 * Execute HyperAMP client operation
 * 
 * @param argc Number of arguments
 * @param argv Array of arguments:
 *             argv[0] - SHM JSON configuration path
 *             argv[1] - Input data (string or @filename)
 *             argv[2] - Service ID (1=encrypt, 2=decrypt, etc.)
 * @return 0 on success, -1 on error
 */
int hyperamp_client(int argc, char* argv[]);

#endif // HYPERAMP_CLIENT_H

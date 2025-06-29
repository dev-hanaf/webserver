#pragma once

#include "BlockDirective.hpp"

class Server : public BlockDirective {
	private:
		bool isDefaultServer;

		Server(const Server& other);
		Server& operator=(const Server& other);

	public:
		Server(void);
		~Server(void);
		DIRTYPE getType(void) const;

		void setIsDefaultServer(bool value);
};

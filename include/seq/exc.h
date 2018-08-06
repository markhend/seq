#ifndef SEQ_EXC_H
#define SEQ_EXC_H

#include <string>
#include <exception>
#include <stdexcept>

namespace seq {
	class Stage;

	namespace exc {
		class SeqException : public std::runtime_error {
		public:
			explicit SeqException(std::string msg);
		};

		class StageException : public std::runtime_error {
		public:
			StageException(std::string msg, Stage& stage);
		};

		class ValidationException : public StageException {
		public:
			explicit ValidationException(Stage& stage);
		};

		class IOException : public std::runtime_error {
		public:
			explicit IOException(std::string msg);
		};
	}
}

#endif /* SEQ_EXC_H */

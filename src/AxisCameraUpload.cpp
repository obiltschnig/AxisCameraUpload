//
// AxisCameraUpload.cpp
//
// A simple HTTP server that accepts image uploads from an Axis network camera.
//
// SPDX-License-Identifier: MIT
//


#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPRequestHandlerFactory.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTTPBasicCredentials.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/NumberFormatter.h"
#include "Poco/StreamCopier.h"
#include "Poco/NullStream.h"
#include "Poco/Exception.h"
#include "Poco/LocalDateTime.h"
#include "Poco/DateTimeFormatter.h"
#include "Poco/FileStream.h"
#include "Poco/Path.h"
#include "Poco/File.h"
#include <iostream>


using namespace std::string_literals;


class ImageUploadRequestHandler: public Poco::Net::HTTPRequestHandler
{
public:
	void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response)
	{
		auto& app = Poco::Util::Application::instance();
		const auto& config = app.config();

		app.logger().information("Request from %s: %s %s"s, request.clientAddress().toString(), request.getMethod(), request.getURI());

		try
		{
			if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_POST)
			{
				if (authenticate(request, config.getString("upload.username"s), config.getString("upload.password"s)))
				{
					if (request.getContentType() == "image/jpeg")
					{
						std::string path = storeImage(request, config.getString("upload.path"s, Poco::Path::current()));
						app.logger().information("Image stored to '%s'."s, path);
						return sendResponse(request, Poco::Net::HTTPResponse::HTTP_OK, "Image accepted"s);
					}
					else
					{
						ignoreContent(request);
						return sendResponse(request, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "Unexpected content type"s);
					}
				}
				else
				{
					app.logger().warning("Unauthenticated request from %s: %s %s"s, request.clientAddress().toString(), request.getMethod(), request.getURI());
					ignoreContent(request);
					response.requireAuthentication("ImageUpload");
					response.send();
					return;
				}
			}
			else if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_GET)
			{
				return sendResponse(request, Poco::Net::HTTPResponse::HTTP_OK, "Image upload server ready"s);
			}
			else
			{
				return sendResponse(request, Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED, "Request method not allowed"s);
			}
		}
		catch (Poco::Exception& exc)
		{
			app.logger().log(exc);
			if (!response.sent())
			{
				sendResponse(request, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "error uploading file");
			}
		}
	}

	void ignoreContent(Poco::Net::HTTPServerRequest& request)
	{
		Poco::NullOutputStream nullStream;
		Poco::StreamCopier::copyStream(request.stream(), nullStream);
	}

	std::string storeImage(Poco::Net::HTTPServerRequest& request, const std::string& uploadPath)
	{
		Poco::Path p(uploadPath);
		p.makeDirectory();
		p.pushDirectory(uploadSite(request));
		p.pushDirectory(uploadCamera(request));

		Poco::LocalDateTime now;
		p.pushDirectory(Poco::NumberFormatter::format(now.year()));
		p.pushDirectory(Poco::NumberFormatter::format0(now.month(), 2));
		p.pushDirectory(Poco::NumberFormatter::format0(now.day(), 2));
		p.pushDirectory(Poco::NumberFormatter::format0(now.hour(), 2));

		Poco::File dir(p.toString());
		dir.createDirectories();

		p.setFileName(Poco::DateTimeFormatter::format(now, "%Y%m%d-%H%M%S-%F.jpg"s));

		Poco::FileOutputStream fileStream(p.toString());
		Poco::StreamCopier::copyStream(request.stream(), fileStream);

		return p.toString();
	}

	std::string uploadSite(const Poco::Net::HTTPServerRequest& request) const
	{
		Poco::Path p(request.getURI(), Poco::Path::PATH_UNIX);
		p.makeDirectory();
		if (p.depth() >= 2)
		{
			return p[1];
		}
		else
		{
			return "defaultSite";
		}
	}

	std::string uploadCamera(const Poco::Net::HTTPServerRequest& request) const
	{
		Poco::Path p(request.getURI(), Poco::Path::PATH_UNIX);
		p.makeDirectory();
		if (p.depth() >= 3)
		{
			return p[2];
		}
		else
		{
			return "defaultCamera";
		}
	}

	bool authenticate(const Poco::Net::HTTPServerRequest& request, const std::string& username, const std::string& password) const
	{
		if (request.hasCredentials())
		{
			std::string scheme;
			std::string authInfo;
			request.getCredentials(scheme, authInfo);
			Poco::Net::HTTPBasicCredentials creds(authInfo);
			return creds.getUsername() == username && creds.getPassword() == password;
		}
		else return false;
	}

	void sendResponse(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPResponse::HTTPStatus status, const std::string& message) const
	{
		request.response().setContentType("text/html"s);
		request.response().setStatusAndReason(status);

		std::string html("<!DOCTYPE html>\n<html><head><title>");
		html += Poco::NumberFormatter::format(static_cast<int>(status));
		html += " - ";
		html += request.response().getReasonForStatus(status);
		html += "</title></head><body><header><h1>"s;
		html += Poco::NumberFormatter::format(static_cast<int>(status));
		html += " - ";
		html += request.response().getReasonForStatus(status);
		html += "</h1></header><section><p>"s;
		html += Poco::Net::htmlize(message);
		html += "</p></section>"s;
		html += "</body></html>"s;
		request.response().sendBuffer(html.data(), html.size());
	}
};


class ImageUploadRequestHandlerFactory: public Poco::Net::HTTPRequestHandlerFactory
{
public:
	Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest& request)
	{
		return new ImageUploadRequestHandler;
	}
};


class ImageUploadServer: public Poco::Util::ServerApplication
{
protected:
	void initialize(Poco::Util::Application& self)
	{
		loadConfiguration(); // load default configuration files, if present
		Poco::Util::ServerApplication::initialize(self);
	}

	void uninitialize()
	{
		Poco::Util::ServerApplication::uninitialize();
	}

	void defineOptions(Poco::Util::OptionSet& options)
	{
		Poco::Util::ServerApplication::defineOptions(options);

		options.addOption(
			Poco::Util::Option("help", "h", "Display help information on command line arguments.")
				.required(false)
				.repeatable(false)
				.callback(Poco::Util::OptionCallback<ImageUploadServer>(this, &ImageUploadServer::handleHelp)));

		options.addOption(
			Poco::Util::Option("config-file", "c", "Load configuration data from a file.")
				.required(false)
				.repeatable(true)
				.argument("file")
				.callback(Poco::Util::OptionCallback<ImageUploadServer>(this, &ImageUploadServer::handleConfig)));
	}

	void handleHelp(const std::string& name, const std::string& value)
	{
		_showHelp = true;
		displayHelp();
		stopOptionsProcessing();
	}

	void handleConfig(const std::string& name, const std::string& value)
	{
		loadConfiguration(value);
	}

	void displayHelp()
	{
		Poco::Util::HelpFormatter helpFormatter(options());
		helpFormatter.setCommand(commandName());
		helpFormatter.setUsage("OPTIONS");
		helpFormatter.setHeader("Image upload server for Axis network cameras.");
		helpFormatter.format(std::cout);
	}

	int main(const std::vector<std::string>& args)
	{
		if (!_showHelp)
		{
			Poco::UInt16 port = static_cast<Poco::UInt16>(config().getInt("http.port"s, 9980));

			Poco::Net::ServerSocket svs(port);
			Poco::Net::HTTPServer srv(new ImageUploadRequestHandlerFactory, svs, new Poco::Net::HTTPServerParams);
			srv.start();
			waitForTerminationRequest();
			srv.stop();
		}
		return Application::EXIT_OK;
	}

private:
	bool _showHelp = false;
};


POCO_SERVER_MAIN(ImageUploadServer)
